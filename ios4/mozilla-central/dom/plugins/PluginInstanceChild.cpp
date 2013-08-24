/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
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
 * The Original Code is Mozilla Plugin App.
 *
 * The Initial Developer of the Original Code is
 *   Chris Jones <jones.chris.g@gmail.com>
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Jim Mathies <jmathies@mozilla.com>
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

#include "PluginInstanceChild.h"
#include "PluginModuleChild.h"
#include "BrowserStreamChild.h"
#include "PluginStreamChild.h"
#include "StreamNotifyChild.h"
#include "PluginProcessChild.h"

#include "mozilla/ipc/SyncChannel.h"

using mozilla::ipc::ProcessChild;
using namespace mozilla::plugins;

#ifdef MOZ_WIDGET_GTK2

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include "gtk2xtbin.h"

#elif defined(MOZ_WIDGET_QT)
#include <QX11Info>
#elif defined(OS_WIN)
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL     0x020E
#endif

#include "nsWindowsDllInterceptor.h"

typedef BOOL (WINAPI *User32TrackPopupMenu)(HMENU hMenu,
                                            UINT uFlags,
                                            int x,
                                            int y,
                                            int nReserved,
                                            HWND hWnd,
                                            CONST RECT *prcRect);
static WindowsDllInterceptor sUser32Intercept;
static HWND sWinlessPopupSurrogateHWND = NULL;
static User32TrackPopupMenu sUser32TrackPopupMenuStub = NULL;

using mozilla::gfx::SharedDIB;

#include <windows.h>
#include <windowsx.h>

// Flash WM_USER message delay time for PostDelayedTask. Borrowed
// from Chromium's web plugin delegate src. See 'flash msg throttling
// helpers' section for details.
const int kFlashWMUSERMessageThrottleDelayMs = 5;

#elif defined(XP_MACOSX)
#include <ApplicationServices/ApplicationServices.h>
#endif // defined(XP_MACOSX)

PluginInstanceChild::PluginInstanceChild(const NPPluginFuncs* aPluginIface,
                                         const nsCString& aMimeType)
    : mPluginIface(aPluginIface)
    , mQuirks(0)
    , mCachedWindowActor(nsnull)
    , mCachedElementActor(nsnull)
#if defined(OS_WIN)
    , mPluginWindowHWND(0)
    , mPluginWndProc(0)
    , mPluginParentHWND(0)
    , mCachedWinlessPluginHWND(0)
    , mWinlessPopupSurrogateHWND(0)
    , mWinlessThrottleOldWndProc(0)
    , mWinlessHiddenMsgHWND(0)
#endif // OS_WIN
    , mAsyncCallMutex("PluginInstanceChild::mAsyncCallMutex")
#if defined(OS_MACOSX)  
    , mShColorSpace(nsnull)
    , mShContext(nsnull)
    , mDrawingModel(NPDrawingModelCoreGraphics)
    , mCurrentEvent(nsnull)
#endif
{
    memset(&mWindow, 0, sizeof(mWindow));
    mData.ndata = (void*) this;
    mData.pdata = nsnull;
#if defined(MOZ_X11) && defined(XP_UNIX) && !defined(XP_MACOSX)
    mWindow.ws_info = &mWsInfo;
    memset(&mWsInfo, 0, sizeof(mWsInfo));
    mWsInfo.display = DefaultXDisplay();
#endif // MOZ_X11 && XP_UNIX && !XP_MACOSX
#if defined(OS_WIN)
    memset(&mAlphaExtract, 0, sizeof(mAlphaExtract));
#endif // OS_WIN
    InitQuirksModes(aMimeType);
#if defined(OS_WIN)
    InitPopupMenuHook();
#endif // OS_WIN
}

PluginInstanceChild::~PluginInstanceChild()
{
#if defined(OS_WIN)
  DestroyPluginWindow();
#endif
#if defined(OS_MACOSX)
    if (mShColorSpace) {
        ::CGColorSpaceRelease(mShColorSpace);
    }
    if (mShContext) {
        ::CGContextRelease(mShContext);
    }
#endif
}

void
PluginInstanceChild::InitQuirksModes(const nsCString& aMimeType)
{
#ifdef OS_WIN
    // application/x-silverlight
    // application/x-silverlight-2
    NS_NAMED_LITERAL_CSTRING(silverlight, "application/x-silverlight");
    // application/x-shockwave-flash
    NS_NAMED_LITERAL_CSTRING(flash, "application/x-shockwave-flash");
    if (FindInReadable(silverlight, aMimeType)) {
        mQuirks |= QUIRK_SILVERLIGHT_WINLESS_INPUT_TRANSLATION;
        mQuirks |= QUIRK_WINLESS_TRACKPOPUP_HOOK;
    }
    else if (FindInReadable(flash, aMimeType)) {
        mQuirks |= QUIRK_WINLESS_TRACKPOPUP_HOOK;
        mQuirks |= QUIRK_FLASH_THROTTLE_WMUSER_EVENTS; 
    }
#endif
}

NPError
PluginInstanceChild::InternalGetNPObjectForValue(NPNVariable aValue,
                                                 NPObject** aObject)
{
    PluginScriptableObjectChild* actor;
    NPError result = NPERR_NO_ERROR;

    switch (aValue) {
        case NPNVWindowNPObject:
            if (!(actor = mCachedWindowActor)) {
                PPluginScriptableObjectChild* actorProtocol;
                CallNPN_GetValue_NPNVWindowNPObject(&actorProtocol, &result);
                if (result == NPERR_NO_ERROR) {
                    actor = mCachedWindowActor =
                        static_cast<PluginScriptableObjectChild*>(actorProtocol);
                    NS_ASSERTION(actor, "Null actor!");
                    PluginModuleChild::sBrowserFuncs.retainobject(
                        actor->GetObject(false));
                }
            }
            break;

        case NPNVPluginElementNPObject:
            if (!(actor = mCachedElementActor)) {
                PPluginScriptableObjectChild* actorProtocol;
                CallNPN_GetValue_NPNVPluginElementNPObject(&actorProtocol,
                                                           &result);
                if (result == NPERR_NO_ERROR) {
                    actor = mCachedElementActor =
                        static_cast<PluginScriptableObjectChild*>(actorProtocol);
                    NS_ASSERTION(actor, "Null actor!");
                    PluginModuleChild::sBrowserFuncs.retainobject(
                        actor->GetObject(false));
                }
            }
            break;

        default:
            NS_NOTREACHED("Don't know what to do with this value type!");
    }

#ifdef DEBUG
    {
        NPError currentResult;
        PPluginScriptableObjectChild* currentActor;

        switch (aValue) {
            case NPNVWindowNPObject:
                CallNPN_GetValue_NPNVWindowNPObject(&currentActor,
                                                    &currentResult);
                break;
            case NPNVPluginElementNPObject:
                CallNPN_GetValue_NPNVPluginElementNPObject(&currentActor,
                                                           &currentResult);
                break;
            default:
                NS_NOTREACHED("Don't know what to do with this value type!");
        }

        // Make sure that the current actor returned by the parent matches our
        // cached actor!
        NS_ASSERTION(static_cast<PluginScriptableObjectChild*>(currentActor) ==
                     actor, "Cached actor is out of date!");
        NS_ASSERTION(currentResult == result, "Results don't match?!");
    }
#endif

    if (result != NPERR_NO_ERROR) {
        return result;
    }

    NPObject* object = actor->GetObject(false);
    NS_ASSERTION(object, "Null object?!");

    *aObject = PluginModuleChild::sBrowserFuncs.retainobject(object);
    return NPERR_NO_ERROR;

}

NPError
PluginInstanceChild::NPN_GetValue(NPNVariable aVar,
                                  void* aValue)
{
    PLUGIN_LOG_DEBUG(("%s (aVar=%i)", FULLFUNCTION, (int) aVar));
    AssertPluginThread();

    switch(aVar) {

    case NPNVSupportsWindowless:
#if defined(OS_LINUX) || defined(OS_WIN)
        *((NPBool*)aValue) = true;
#else
        *((NPBool*)aValue) = false;
#endif
        return NPERR_NO_ERROR;

#if defined(OS_LINUX)
    case NPNVSupportsXEmbedBool:
        *((NPBool*)aValue) = true;
        return NPERR_NO_ERROR;

    case NPNVToolkit:
        *((NPNToolkitType*)aValue) = NPNVGtk2;
        return NPERR_NO_ERROR;

#elif defined(OS_WIN)
    case NPNVToolkit:
        return NPERR_GENERIC_ERROR;
#endif
    case NPNVjavascriptEnabledBool: {
        bool v = false;
        NPError result;
        if (!CallNPN_GetValue_NPNVjavascriptEnabledBool(&v, &result)) {
            return NPERR_GENERIC_ERROR;
        }
        *static_cast<NPBool*>(aValue) = v;
        return result;
    }

    case NPNVisOfflineBool: {
        bool v = false;
        NPError result;
        if (!CallNPN_GetValue_NPNVisOfflineBool(&v, &result)) {
            return NPERR_GENERIC_ERROR;
        }
        *static_cast<NPBool*>(aValue) = v;
        return result;
    }

    case NPNVprivateModeBool: {
        bool v = false;
        NPError result;
        if (!CallNPN_GetValue_NPNVprivateModeBool(&v, &result)) {
            return NPERR_GENERIC_ERROR;
        }
        *static_cast<NPBool*>(aValue) = v;
        return result;
    }

    case NPNVWindowNPObject: // Intentional fall-through
    case NPNVPluginElementNPObject: {
        NPObject* object;
        NPError result = InternalGetNPObjectForValue(aVar, &object);
        if (result == NPERR_NO_ERROR) {
            *((NPObject**)aValue) = object;
        }
        return result;
    }

    case NPNVnetscapeWindow: {
#ifdef XP_WIN
        if (mWindow.type == NPWindowTypeDrawable) {
            if (mCachedWinlessPluginHWND) {
              *static_cast<HWND*>(aValue) = mCachedWinlessPluginHWND;
              return NPERR_NO_ERROR;
            }
            NPError result;
            if (!CallNPN_GetValue_NPNVnetscapeWindow(&mCachedWinlessPluginHWND, &result)) {
                return NPERR_GENERIC_ERROR;
            }
            *static_cast<HWND*>(aValue) = mCachedWinlessPluginHWND;
            return result;
        }
        else {
            *static_cast<HWND*>(aValue) = mPluginWindowHWND;
            return NPERR_NO_ERROR;
        }
#elif defined(MOZ_X11)
        NPError result;
        CallNPN_GetValue_NPNVnetscapeWindow(static_cast<XID*>(aValue), &result);
        return result;
#else
        return NPERR_GENERIC_ERROR;
#endif
    }

#ifdef XP_MACOSX
   case NPNVsupportsCoreGraphicsBool: {
        *((NPBool*)aValue) = true;
        return NPERR_NO_ERROR;
    }

    case NPNVsupportsCoreAnimationBool: {
        *((NPBool*)aValue) = true;
        return NPERR_NO_ERROR;
    }

    case NPNVsupportsInvalidatingCoreAnimationBool: {
        *((NPBool*)aValue) = true;
        return NPERR_NO_ERROR;
    }

    case NPNVsupportsCocoaBool: {
        *((NPBool*)aValue) = true;
        return NPERR_NO_ERROR;
    }
  
#ifndef NP_NO_QUICKDRAW
    case NPNVsupportsQuickDrawBool: {
        *((NPBool*)aValue) = false;
        return NPERR_NO_ERROR;
    }
#endif /* NP_NO_QUICKDRAW */
#endif /* XP_MACOSX */

    default:
        PR_LOG(gPluginLog, PR_LOG_WARNING,
               ("In PluginInstanceChild::NPN_GetValue: Unhandled NPNVariable %i (%s)",
                (int) aVar, NPNVariableToString(aVar)));
        return NPERR_GENERIC_ERROR;
    }

}


NPError
PluginInstanceChild::NPN_SetValue(NPPVariable aVar, void* aValue)
{
    PR_LOG(gPluginLog, PR_LOG_DEBUG, ("%s (aVar=%i, aValue=%p)",
                                      FULLFUNCTION, (int) aVar, aValue));

    AssertPluginThread();

    switch (aVar) {
    case NPPVpluginWindowBool: {
        NPError rv;
        bool windowed = (NPBool) (intptr_t) aValue;

        if (!CallNPN_SetValue_NPPVpluginWindow(windowed, &rv))
            return NPERR_GENERIC_ERROR;

        return rv;
    }

    case NPPVpluginTransparentBool: {
        NPError rv;
        bool transparent = (NPBool) (intptr_t) aValue;

        if (!CallNPN_SetValue_NPPVpluginTransparent(transparent, &rv))
            return NPERR_GENERIC_ERROR;

        return rv;
    }

#ifdef XP_MACOSX
    case NPPVpluginDrawingModel: {
        NPError rv;
        int drawingModel = (int16) (intptr_t) aValue;

        if (!CallNPN_SetValue_NPPVpluginDrawingModel(drawingModel, &rv))
            return NPERR_GENERIC_ERROR;
        mDrawingModel = drawingModel;

        return rv;
    }

    case NPPVpluginEventModel: {
        NPError rv;
        int eventModel = (int16) (intptr_t) aValue;

        if (!CallNPN_SetValue_NPPVpluginEventModel(eventModel, &rv))
            return NPERR_GENERIC_ERROR;

        return rv;
    }
#endif

    default:
        PR_LOG(gPluginLog, PR_LOG_WARNING,
               ("In PluginInstanceChild::NPN_SetValue: Unhandled NPPVariable %i (%s)",
                (int) aVar, NPPVariableToString(aVar)));
        return NPERR_GENERIC_ERROR;
    }
}

bool
PluginInstanceChild::AnswerNPP_GetValue_NPPVpluginNeedsXEmbed(
    bool* needs, NPError* rv)
{
    AssertPluginThread();

#ifdef MOZ_X11
    // The documentation on the types for many variables in NP(N|P)_GetValue
    // is vague.  Often boolean values are NPBool (1 byte), but
    // https://developer.mozilla.org/en/XEmbed_Extension_for_Mozilla_Plugins
    // treats NPPVpluginNeedsXEmbed as PRBool (int), and
    // on x86/32-bit, flash stores to this using |movl 0x1,&needsXEmbed|.
    // thus we can't use NPBool for needsXEmbed, or the three bytes above
    // it on the stack would get clobbered. so protect with the larger PRBool.
    PRBool needsXEmbed = 0;
    if (!mPluginIface->getvalue) {
        *rv = NPERR_GENERIC_ERROR;
    }
    else {
        *rv = mPluginIface->getvalue(GetNPP(), NPPVpluginNeedsXEmbed,
                                     &needsXEmbed);
    }
    *needs = needsXEmbed;
    return true;

#else

    NS_RUNTIMEABORT("shouldn't be called on non-X11 platforms");
    return false;               // not reached

#endif
}

bool
PluginInstanceChild::AnswerNPP_GetValue_NPPVpluginScriptableNPObject(
                                          PPluginScriptableObjectChild** aValue,
                                          NPError* aResult)
{
    AssertPluginThread();

    NPObject* object = nsnull;
    NPError result = NPERR_GENERIC_ERROR;
    if (mPluginIface->getvalue) {
        result = mPluginIface->getvalue(GetNPP(), NPPVpluginScriptableNPObject,
                                        &object);
    }
    if (result == NPERR_NO_ERROR && object) {
        PluginScriptableObjectChild* actor = GetActorForNPObject(object);

        // If we get an actor then it has retained. Otherwise we don't need it
        // any longer.
        PluginModuleChild::sBrowserFuncs.releaseobject(object);
        if (actor) {
            *aValue = actor;
            *aResult = NPERR_NO_ERROR;
            return true;
        }

        NS_ERROR("Failed to get actor!");
        result = NPERR_GENERIC_ERROR;
    }
    else {
        result = NPERR_GENERIC_ERROR;
    }

    *aValue = nsnull;
    *aResult = result;
    return true;
}

bool
PluginInstanceChild::AnswerNPP_SetValue_NPNVprivateModeBool(const bool& value,
                                                            NPError* result)
{
    if (!mPluginIface->setvalue) {
        *result = NPERR_GENERIC_ERROR;
        return true;
    }

    NPBool v = value;
    *result = mPluginIface->setvalue(GetNPP(), NPNVprivateModeBool, &v);
    return true;
}

bool
PluginInstanceChild::AnswerNPP_HandleEvent(const NPRemoteEvent& event,
                                           int16_t* handled)
{
    PLUGIN_LOG_DEBUG_FUNCTION;
    AssertPluginThread();

#if defined(MOZ_X11) && defined(DEBUG)
    if (GraphicsExpose == event.event.type)
        PLUGIN_LOG_DEBUG(("  received drawable 0x%lx\n",
                          event.event.xgraphicsexpose.drawable));
#endif

#ifdef XP_MACOSX
    // Mac OS X does not define an NPEvent structure. It defines more specific types.
    NPCocoaEvent evcopy = event.event;

    // Make sure we reset mCurrentEvent in case of an exception
    AutoRestore<const NPCocoaEvent*> savePreviousEvent(mCurrentEvent);

    // Track the current event for NPN_PopUpContextMenu.
    mCurrentEvent = &event.event;
#else
    // Make a copy since we may modify values.
    NPEvent evcopy = event.event;
#endif

#ifdef OS_WIN
    // FIXME/bug 567645: temporarily drop the "dummy event" on the floor
    if (WM_NULL == evcopy.event)
        return true;

    // Painting for win32. SharedSurfacePaint handles everything.
    if (mWindow.type == NPWindowTypeDrawable) {
       if (evcopy.event == WM_PAINT) {
          *handled = SharedSurfacePaint(evcopy);
          return true;
       }
       else if (DoublePassRenderingEvent() == evcopy.event) {
            // We'll render to mSharedSurfaceDib first, then render to a cached bitmap
            // we store locally. The two passes are for alpha extraction, so the second
            // pass must be to a flat white surface in order for things to work.
            mAlphaExtract.doublePass = RENDER_BACK_ONE;
            *handled = true;
            return true;
       }
    }
    *handled = WinlessHandleEvent(evcopy);
    return true;
#endif

    if (!mPluginIface->event)
        *handled = false;
    else
        *handled = mPluginIface->event(&mData, reinterpret_cast<void*>(&evcopy));

#ifdef XP_MACOSX
    // Release any reference counted objects created in the child process.
    if (evcopy.type == NPCocoaEventKeyDown ||
        evcopy.type == NPCocoaEventKeyUp) {
      ::CFRelease((CFStringRef)evcopy.data.key.characters);
      ::CFRelease((CFStringRef)evcopy.data.key.charactersIgnoringModifiers);
    }
    else if (evcopy.type == NPCocoaEventTextInput) {
      ::CFRelease((CFStringRef)evcopy.data.text.text);
    }
#endif

#ifdef MOZ_X11
    if (GraphicsExpose == event.event.type) {
        // Make sure the X server completes the drawing before the parent
        // draws on top and destroys the Drawable.
        //
        // XSync() waits for the X server to complete.  Really this child
        // process does not need to wait; the parent is the process that needs
        // to wait.  A possibly-slightly-better alternative would be to send
        // an X event to the parent that the parent would wait for.
        XSync(mWsInfo.display, False);
    }
#endif

    return true;
}

#ifdef XP_MACOSX

bool
PluginInstanceChild::AnswerNPP_HandleEvent_Shmem(const NPRemoteEvent& event,
                                                 Shmem& mem,
                                                 int16_t* handled,
                                                 Shmem* rtnmem)
{
    PLUGIN_LOG_DEBUG_FUNCTION;
    AssertPluginThread();

    PaintTracker pt;

    NPCocoaEvent evcopy = event.event;

    if (evcopy.type == NPCocoaEventDrawRect) {
        if (!mShColorSpace) {
            mShColorSpace = CreateSystemColorSpace();
            if (!mShColorSpace) {
                PLUGIN_LOG_DEBUG(("Could not allocate ColorSpace."));
                *handled = false;
                *rtnmem = mem;
                return true;
            } 
        }
        if (!mShContext) {
            void* cgContextByte = mem.get<char>();
            mShContext = ::CGBitmapContextCreate(cgContextByte, 
                              mWindow.width, mWindow.height, 8, 
                              mWindow.width * 4, mShColorSpace, 
                              kCGImageAlphaPremultipliedFirst |
                              kCGBitmapByteOrder32Host);
    
            if (!mShContext) {
                PLUGIN_LOG_DEBUG(("Could not allocate CGBitmapContext."));
                *handled = false;
                *rtnmem = mem;
                return true;
            }
        }
        CGRect clearRect = ::CGRectMake(0, 0, mWindow.width, mWindow.height);
        ::CGContextClearRect(mShContext, clearRect);
        evcopy.data.draw.context = mShContext; 
    } else {
        PLUGIN_LOG_DEBUG(("Invalid event type for AnswerNNP_HandleEvent_Shmem."));
        *handled = false;
        *rtnmem = mem;
        return true;
    } 

    if (!mPluginIface->event) {
        *handled = false;
    } else {
        ::CGContextSaveGState(evcopy.data.draw.context);
        *handled = mPluginIface->event(&mData, reinterpret_cast<void*>(&evcopy));
        ::CGContextRestoreGState(evcopy.data.draw.context);
    }

    *rtnmem = mem;
    return true;
}

#else
bool
PluginInstanceChild::AnswerNPP_HandleEvent_Shmem(const NPRemoteEvent& event,
                                                 Shmem& mem,
                                                 int16_t* handled,
                                                 Shmem* rtnmem)
{
    NS_RUNTIMEABORT("not reached.");
    *rtnmem = mem;
    return true;
}
#endif

#ifdef XP_MACOSX
bool
PluginInstanceChild::AnswerNPP_HandleEvent_IOSurface(const NPRemoteEvent& event,
                                                     const uint32_t &surfaceid,
                                                     int16_t* handled)
{
    PLUGIN_LOG_DEBUG_FUNCTION;
    AssertPluginThread();

    PaintTracker pt;

    NPCocoaEvent evcopy = event.event;
    nsIOSurface* surf = nsIOSurface::LookupSurface(surfaceid);
    if (!surf) {
        NS_ERROR("Invalid IOSurface.");
        *handled = false;
        return false;
    }

    if (evcopy.type == NPCocoaEventDrawRect) {
        mCARenderer.AttachIOSurface(surf);
        if (!mCARenderer.isInit()) {
            void *caLayer = nsnull;
            NPError result = mPluginIface->getvalue(GetNPP(), 
                                     NPPVpluginCoreAnimationLayer,
                                     &caLayer);
            if (result != NPERR_NO_ERROR || !caLayer) {
                PLUGIN_LOG_DEBUG(("Plugin requested CoreAnimation but did not "
                                  "provide CALayer."));
                *handled = false;
                return false;
            }
            mCARenderer.SetupRenderer(caLayer, mWindow.width, mWindow.height);
            // Flash needs to have the window set again after this step
            if (mPluginIface->setwindow)
                (void) mPluginIface->setwindow(&mData, &mWindow);
        }
    } else {
        PLUGIN_LOG_DEBUG(("Invalid event type for "
                          "AnswerNNP_HandleEvent_IOSurface."));
        *handled = false;
        return false;
    } 

    mCARenderer.Render(mWindow.width, mWindow.height, nsnull);

    return true;

}

#else
bool
PluginInstanceChild::AnswerNPP_HandleEvent_IOSurface(const NPRemoteEvent& event,
                                                     const uint32_t &surfaceid,
                                                     int16_t* handled)
{
    NS_RUNTIMEABORT("NPP_HandleEvent_IOSurface is a OSX-only message");
    return false;
}
#endif

bool
PluginInstanceChild::RecvWindowPosChanged(const NPRemoteEvent& event)
{
#ifdef OS_WIN
    int16_t dontcare;
    return AnswerNPP_HandleEvent(event, &dontcare);
#else
    NS_RUNTIMEABORT("WindowPosChanged is a windows-only message");
    return false;
#endif
}


#if defined(MOZ_X11) && defined(XP_UNIX) && !defined(XP_MACOSX)
static bool
XVisualIDToInfo(Display* aDisplay, VisualID aVisualID,
                Visual** aVisual, unsigned int* aDepth)
{
    if (aVisualID == None) {
        *aVisual = NULL;
        *aDepth = 0;
        return true;
    }

    const Screen* screen = DefaultScreenOfDisplay(aDisplay);

    for (int d = 0; d < screen->ndepths; d++) {
        Depth *d_info = &screen->depths[d];
        for (int v = 0; v < d_info->nvisuals; v++) {
            Visual* visual = &d_info->visuals[v];
            if (visual->visualid == aVisualID) {
                *aVisual = visual;
                *aDepth = d_info->depth;
                return true;
            }
        }
    }

    NS_ERROR("VisualID not on Screen.");
    return false;
}
#endif

bool
PluginInstanceChild::AnswerNPP_SetWindow(const NPRemoteWindow& aWindow)
{
    PLUGIN_LOG_DEBUG(("%s (aWindow=<window: 0x%lx, x: %d, y: %d, width: %d, height: %d>)",
                      FULLFUNCTION,
                      aWindow.window,
                      aWindow.x, aWindow.y,
                      aWindow.width, aWindow.height));
    AssertPluginThread();

#if defined(MOZ_X11) && defined(XP_UNIX) && !defined(XP_MACOSX)
    // The minimum info is sent over IPC to allow this
    // code to determine the rest.

    mWindow.window = reinterpret_cast<void*>(aWindow.window);
    mWindow.x = aWindow.x;
    mWindow.y = aWindow.y;
    mWindow.width = aWindow.width;
    mWindow.height = aWindow.height;
    mWindow.clipRect = aWindow.clipRect;
    mWindow.type = aWindow.type;

    mWsInfo.colormap = aWindow.colormap;
    if (!XVisualIDToInfo(mWsInfo.display, aWindow.visualID,
                         &mWsInfo.visual, &mWsInfo.depth))
        return false;

#ifdef MOZ_WIDGET_GTK2
    if (gtk_check_version(2,18,7) != NULL) { // older
        if (aWindow.type == NPWindowTypeWindow) {
            GdkWindow* socket_window = gdk_window_lookup(aWindow.window);
            if (socket_window) {
                // A GdkWindow for the socket already exists.  Need to
                // workaround https://bugzilla.gnome.org/show_bug.cgi?id=607061
                // See wrap_gtk_plug_embedded in PluginModuleChild.cpp.
                g_object_set_data(G_OBJECT(socket_window),
                                  "moz-existed-before-set-window",
                                  GUINT_TO_POINTER(1));
            }
        }

        if (aWindow.visualID != None
            && gtk_check_version(2, 12, 10) != NULL) { // older
            // Workaround for a bug in Gtk+ (prior to 2.12.10) where deleting
            // a foreign GdkColormap will also free the XColormap.
            // http://git.gnome.org/browse/gtk+/log/gdk/x11/gdkcolor-x11.c?id=GTK_2_12_10
            GdkVisual *gdkvisual = gdkx_visual_get(aWindow.visualID);
            GdkColormap *gdkcolor =
                gdk_x11_colormap_foreign_new(gdkvisual, aWindow.colormap);

            if (g_object_get_data(G_OBJECT(gdkcolor), "moz-have-extra-ref")) {
                // We already have a ref to keep the object alive.
                g_object_unref(gdkcolor);
            } else {
                // leak and mark as already leaked
                g_object_set_data(G_OBJECT(gdkcolor),
                                  "moz-have-extra-ref", GUINT_TO_POINTER(1));
            }
        }
    }
#endif

    if (mPluginIface->setwindow)
        (void) mPluginIface->setwindow(&mData, &mWindow);

#elif defined(OS_WIN)
    switch (aWindow.type) {
      case NPWindowTypeWindow:
      {
          if (!CreatePluginWindow())
              return false;

          ReparentPluginWindow((HWND)aWindow.window);
          SizePluginWindow(aWindow.width, aWindow.height);

          mWindow.window = (void*)mPluginWindowHWND;
          mWindow.x = aWindow.x;
          mWindow.y = aWindow.y;
          mWindow.width = aWindow.width;
          mWindow.height = aWindow.height;
          mWindow.type = aWindow.type;

          if (mPluginIface->setwindow) {
              (void) mPluginIface->setwindow(&mData, &mWindow);
              WNDPROC wndProc = reinterpret_cast<WNDPROC>(
                  GetWindowLongPtr(mPluginWindowHWND, GWLP_WNDPROC));
              if (wndProc != PluginWindowProc) {
                  mPluginWndProc = reinterpret_cast<WNDPROC>(
                      SetWindowLongPtr(mPluginWindowHWND, GWLP_WNDPROC,
                                       reinterpret_cast<LONG_PTR>(PluginWindowProc)));
              }
          }
      }
      break;

      case NPWindowTypeDrawable:
          mWindow.type = aWindow.type;
          if (mQuirks & QUIRK_WINLESS_TRACKPOPUP_HOOK)
              CreateWinlessPopupSurrogate();
          if (mQuirks & QUIRK_FLASH_THROTTLE_WMUSER_EVENTS)
              SetupFlashMsgThrottle();
          return SharedSurfaceSetWindow(aWindow);
      break;

      default:
          NS_NOTREACHED("Bad plugin window type.");
          return false;
      break;
    }

#elif defined(XP_MACOSX)

    mWindow.x = aWindow.x;
    mWindow.y = aWindow.y;
    mWindow.width = aWindow.width;
    mWindow.height = aWindow.height;
    mWindow.clipRect = aWindow.clipRect;
    mWindow.type = aWindow.type;

    if (mShContext) {
        // Release the shared context so that it is reallocated
        // with the new size. 
        ::CGContextRelease(mShContext);
        mShContext = nsnull;
    }

    if (mPluginIface->setwindow)
        (void) mPluginIface->setwindow(&mData, &mWindow);

#elif defined(ANDROID)
#  warning Need Android impl
#else
#  error Implement me for your OS
#endif

    return true;
}

bool
PluginInstanceChild::Initialize()
{
    return true;
}

#if defined(OS_WIN)

static const TCHAR kWindowClassName[] = TEXT("GeckoPluginWindow");
static const TCHAR kPluginInstanceChildProperty[] = TEXT("PluginInstanceChildProperty");

// static
bool
PluginInstanceChild::RegisterWindowClass()
{
    static bool alreadyRegistered = false;
    if (alreadyRegistered)
        return true;

    alreadyRegistered = true;

    WNDCLASSEX wcex;
    wcex.cbSize         = sizeof(WNDCLASSEX);
    wcex.style          = CS_DBLCLKS;
    wcex.lpfnWndProc    = DummyWindowProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = GetModuleHandle(NULL);
    wcex.hIcon          = 0;
    wcex.hCursor        = 0;
    wcex.hbrBackground  = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wcex.lpszMenuName   = 0;
    wcex.lpszClassName  = kWindowClassName;
    wcex.hIconSm        = 0;

    return RegisterClassEx(&wcex) ? true : false;
}

bool
PluginInstanceChild::CreatePluginWindow()
{
    // already initialized
    if (mPluginWindowHWND)
        return true;
        
    if (!RegisterWindowClass())
        return false;

    mPluginWindowHWND =
        CreateWindowEx(WS_EX_LEFT | WS_EX_LTRREADING |
                       WS_EX_NOPARENTNOTIFY | // XXXbent Get rid of this!
                       WS_EX_RIGHTSCROLLBAR,
                       kWindowClassName, 0,
                       WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                       0, 0, NULL, 0, GetModuleHandle(NULL), 0);
    if (!mPluginWindowHWND)
        return false;
    if (!SetProp(mPluginWindowHWND, kPluginInstanceChildProperty, this))
        return false;

    // Apparently some plugins require an ASCII WndProc.
    SetWindowLongPtrA(mPluginWindowHWND, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(DefWindowProcA));

    return true;
}

void
PluginInstanceChild::DestroyPluginWindow()
{
    if (mPluginWindowHWND) {
        // Unsubclass the window.
        WNDPROC wndProc = reinterpret_cast<WNDPROC>(
            GetWindowLongPtr(mPluginWindowHWND, GWLP_WNDPROC));
        if (wndProc == PluginWindowProc) {
            NS_ASSERTION(mPluginWndProc, "Should have old proc here!");
            SetWindowLongPtr(mPluginWindowHWND, GWLP_WNDPROC,
                             reinterpret_cast<LONG_PTR>(mPluginWndProc));
            mPluginWndProc = 0;
        }

        RemoveProp(mPluginWindowHWND, kPluginInstanceChildProperty);
        DestroyWindow(mPluginWindowHWND);
        mPluginWindowHWND = 0;
    }
}

void
PluginInstanceChild::ReparentPluginWindow(HWND hWndParent)
{
    if (hWndParent != mPluginParentHWND && IsWindow(hWndParent)) {
        // Fix the child window's style to be a child window.
        LONG_PTR style = GetWindowLongPtr(mPluginWindowHWND, GWL_STYLE);
        style |= WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        style &= ~WS_POPUP;
        SetWindowLongPtr(mPluginWindowHWND, GWL_STYLE, style);

        // Do the reparenting.
        SetParent(mPluginWindowHWND, hWndParent);

        // Make sure we're visible.
        ShowWindow(mPluginWindowHWND, SW_SHOWNA);
    }
    mPluginParentHWND = hWndParent;
}

void
PluginInstanceChild::SizePluginWindow(int width,
                                      int height)
{
    if (mPluginWindowHWND) {
        mPluginSize.x = width;
        mPluginSize.y = height;
        SetWindowPos(mPluginWindowHWND, NULL, 0, 0, width, height,
                     SWP_NOZORDER | SWP_NOREPOSITION);
    }
}

// See chromium's webplugin_delegate_impl.cc for explanation of this function.
// static
LRESULT CALLBACK
PluginInstanceChild::DummyWindowProc(HWND hWnd,
                                     UINT message,
                                     WPARAM wParam,
                                     LPARAM lParam)
{
    return CallWindowProc(DefWindowProc, hWnd, message, wParam, lParam);
}

// static
LRESULT CALLBACK
PluginInstanceChild::PluginWindowProc(HWND hWnd,
                                      UINT message,
                                      WPARAM wParam,
                                      LPARAM lParam)
{
    NS_ASSERTION(!mozilla::ipc::SyncChannel::IsPumpingMessages(),
                 "Failed to prevent a nonqueued message from running!");

    PluginInstanceChild* self = reinterpret_cast<PluginInstanceChild*>(
        GetProp(hWnd, kPluginInstanceChildProperty));
    if (!self) {
        NS_NOTREACHED("Badness!");
        return 0;
    }

    NS_ASSERTION(self->mPluginWindowHWND == hWnd, "Wrong window!");

    // Adobe's shockwave positions the plugin window relative to the browser
    // frame when it initializes. With oopp disabled, this wouldn't have an
    // effect. With oopp, GeckoPluginWindow is a child of the parent plugin
    // window, so the move offsets the child within the parent. Generally
    // we don't want plugins moving or sizing our window, so we prevent these
    // changes here.
    if (message == WM_WINDOWPOSCHANGING) {
      WINDOWPOS* pos = reinterpret_cast<WINDOWPOS*>(lParam);
      if (pos && (!(pos->flags & SWP_NOMOVE) || !(pos->flags & SWP_NOSIZE))) {
        pos->x = pos->y = 0;
        pos->cx = self->mPluginSize.x;
        pos->cy = self->mPluginSize.y;
        LRESULT res = CallWindowProc(self->mPluginWndProc, hWnd, message, wParam,
                                     lParam);
        pos->x = pos->y = 0;
        pos->cx = self->mPluginSize.x;
        pos->cy = self->mPluginSize.y;
        return res;
      }
    }

    // The plugin received keyboard focus, let the parent know so the dom is up to date.
    if (message == WM_MOUSEACTIVATE)
      self->CallPluginFocusChange(PR_TRUE);

    // Prevent lockups due to plugins making rpc calls when the parent
    // is making a synchronous SendMessage call to the child window. Add
    // more messages as needed.
    if ((InSendMessageEx(NULL)&(ISMEX_REPLIED|ISMEX_SEND)) == ISMEX_SEND) {
        switch(message) {
            case WM_KILLFOCUS:
            case WM_MOUSEHWHEEL:
            case WM_MOUSEWHEEL:
            case WM_HSCROLL:
            case WM_VSCROLL:
            ReplyMessage(0);
            break;
        }
    }

    if (message == WM_KILLFOCUS)
      self->CallPluginFocusChange(PR_FALSE);

    if (message == WM_USER+1 &&
        (self->mQuirks & PluginInstanceChild::QUIRK_FLASH_THROTTLE_WMUSER_EVENTS)) {
        self->FlashThrottleMessage(hWnd, message, wParam, lParam, true);
        return 0;
    }

    LRESULT res = CallWindowProc(self->mPluginWndProc, hWnd, message, wParam,
                                 lParam);

    if (message == WM_CLOSE)
        self->DestroyPluginWindow();

    if (message == WM_NCDESTROY)
        RemoveProp(hWnd, kPluginInstanceChildProperty);

    return res;
}

/* windowless track popup menu helpers */

BOOL
WINAPI
PluginInstanceChild::TrackPopupHookProc(HMENU hMenu,
                                        UINT uFlags,
                                        int x,
                                        int y,
                                        int nReserved,
                                        HWND hWnd,
                                        CONST RECT *prcRect)
{
  if (!sUser32TrackPopupMenuStub) {
      NS_ERROR("TrackPopupMenu stub isn't set! Badness!");
      return 0;
  }

  // Only change the parent when we know this is a context on the plugin
  // surface within the browser. Prevents resetting the parent on child ui
  // displayed by plugins that have working parent-child relationships.
  PRUnichar szClass[21];
  bool haveClass = GetClassNameW(hWnd, szClass, NS_ARRAY_LENGTH(szClass));
  if (!haveClass || 
      (wcscmp(szClass, L"MozillaWindowClass") &&
       wcscmp(szClass, L"SWFlash_Placeholder"))) {
      // Unrecognized parent
      return sUser32TrackPopupMenuStub(hMenu, uFlags, x, y, nReserved,
                                       hWnd, prcRect);
  }

  // Called on an unexpected event, warn.
  if (!sWinlessPopupSurrogateHWND) {
      NS_WARNING(
          "Untraced TrackPopupHookProc call! Menu might not work right!");
      return sUser32TrackPopupMenuStub(hMenu, uFlags, x, y, nReserved,
                                       hWnd, prcRect);
  }

  HWND surrogateHwnd = sWinlessPopupSurrogateHWND;
  sWinlessPopupSurrogateHWND = NULL;

  // Popups that don't use TPM_RETURNCMD expect a final command message
  // when an item is selected and the context closes. Since we replace
  // the parent, we need to forward this back to the real parent so it
  // can act on the menu item selected.
  bool isRetCmdCall = (uFlags & TPM_RETURNCMD);

  // A little trick scrounged from chromium's code - set the focus
  // to our surrogate parent so keyboard nav events go to the menu. 
  HWND focusHwnd = SetFocus(surrogateHwnd);
  DWORD res = sUser32TrackPopupMenuStub(hMenu, uFlags|TPM_RETURNCMD, x, y,
                                        nReserved, surrogateHwnd, prcRect);
  if (IsWindow(focusHwnd)) {
      SetFocus(focusHwnd);
  }

  if (!isRetCmdCall && res) {
      SendMessage(hWnd, WM_COMMAND, MAKEWPARAM(res, 0), 0);
  }

  return res;
}

void
PluginInstanceChild::InitPopupMenuHook()
{
    if (!(mQuirks & QUIRK_WINLESS_TRACKPOPUP_HOOK) ||
        sUser32TrackPopupMenuStub)
        return;

    // Note, once WindowsDllInterceptor is initialized for a module,
    // it remains initialized for that particular module for it's
    // lifetime. Additional instances are needed if other modules need
    // to be hooked.
    sUser32Intercept.Init("user32.dll");
    sUser32Intercept.AddHook("TrackPopupMenu", TrackPopupHookProc,
                             (void**) &sUser32TrackPopupMenuStub);
}

void
PluginInstanceChild::CreateWinlessPopupSurrogate()
{
    // already initialized
    if (mWinlessPopupSurrogateHWND)
        return;

    HWND hwnd = NULL;
    NPError result;
    if (!CallNPN_GetValue_NPNVnetscapeWindow(&hwnd, &result)) {
        NS_ERROR("CallNPN_GetValue_NPNVnetscapeWindow failed.");
        return;
    }

    mWinlessPopupSurrogateHWND =
        CreateWindowEx(WS_EX_NOPARENTNOTIFY, L"Static", NULL, WS_CHILD, 0, 0,
                       0, 0, hwnd, 0, GetModuleHandle(NULL), 0);
    if (!mWinlessPopupSurrogateHWND) {
        NS_ERROR("CreateWindowEx failed for winless placeholder!");
        return;
    }
    return;
}

void
PluginInstanceChild::DestroyWinlessPopupSurrogate()
{
    if (mWinlessPopupSurrogateHWND)
        DestroyWindow(mWinlessPopupSurrogateHWND);
    mWinlessPopupSurrogateHWND = NULL;
}

/* windowless handle event helpers */

static bool
NeedsNestedEventCoverage(UINT msg)
{
    // Events we assume some sort of modal ui *might* be generated.
    switch (msg) {
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_CONTEXTMENU:
            return true;
    }
    return false;
}

static bool
IsMouseInputEvent(UINT msg)
{
    switch (msg) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
            return true;
    }
    return false;
}

int16_t
PluginInstanceChild::WinlessHandleEvent(NPEvent& event)
{
    if (!mPluginIface->event)
        return false;

    // Winless Silverlight quirk: winposchanged events are not used in
    // determining the position of the plugin within the parent window,
    // NPP_SetWindow values are used instead. Due to shared memory dib
    // rendering, the origin of NPP_SetWindow is 0x0, so we trap
    // winposchanged events here and do the translation internally for
    // mouse input events.
    if (mQuirks & QUIRK_SILVERLIGHT_WINLESS_INPUT_TRANSLATION) {
        if (event.event == WM_WINDOWPOSCHANGED && event.lParam) {
            WINDOWPOS* pos = reinterpret_cast<WINDOWPOS*>(event.lParam);
            mPluginOffset.x = pos->x;
            mPluginOffset.y = pos->y;
        }
        else if (IsMouseInputEvent(event.event)) {
            event.lParam =
                MAKELPARAM((GET_X_LPARAM(event.lParam) - mPluginOffset.x),
                           (GET_Y_LPARAM(event.lParam) - mPluginOffset.y));
        }
    }

    if (!NeedsNestedEventCoverage(event.event)) {
        return mPluginIface->event(&mData, reinterpret_cast<void*>(&event));
    }

    // Events that might generate nested event dispatch loops need
    // special handling during delivery.
    int16_t handled;

    // TrackPopupMenu will fail if the parent window is not associated with
    // our ui thread. So we hook TrackPopupMenu so we can hand in a surrogate
    // parent created in the child process.
    if ((mQuirks & QUIRK_WINLESS_TRACKPOPUP_HOOK) && // XXX turn on by default?
          (event.event == WM_RBUTTONDOWN || // flash
           event.event == WM_RBUTTONUP)) {  // silverlight
      sWinlessPopupSurrogateHWND = mWinlessPopupSurrogateHWND;
    }

    handled = mPluginIface->event(&mData, reinterpret_cast<void*>(&event));

    sWinlessPopupSurrogateHWND = NULL;

    return handled;
}

/* windowless drawing helpers */

bool
PluginInstanceChild::SharedSurfaceSetWindow(const NPRemoteWindow& aWindow)
{
    // If the surfaceHandle is empty, parent is telling us we can reuse our cached
    // memory surface and hdc. Otherwise, we need to reset, usually due to a
    // expanding plugin port size.
    if (!aWindow.surfaceHandle) {
        if (!mSharedSurfaceDib.IsValid()) {
            return false;
        }
    }
    else {
        // Attach to the new shared surface parent handed us.
        if (NS_FAILED(mSharedSurfaceDib.Attach((SharedDIB::Handle)aWindow.surfaceHandle,
                                               aWindow.width, aWindow.height, 32)))
          return false;
        // Free any alpha extraction resources if needed. This will be reset
        // the next time it's used.
        AlphaExtractCacheRelease();
    }
      
    // NPRemoteWindow's origin is the origin of our shared dib.
    mWindow.x      = 0;
    mWindow.y      = 0;
    mWindow.width  = aWindow.width;
    mWindow.height = aWindow.height;
    mWindow.type   = aWindow.type;

    mWindow.window = reinterpret_cast<void*>(mSharedSurfaceDib.GetHDC());

    if (mPluginIface->setwindow)
        mPluginIface->setwindow(&mData, &mWindow);

    return true;
}

void
PluginInstanceChild::SharedSurfaceRelease()
{
    mSharedSurfaceDib.Close();
    AlphaExtractCacheRelease();
}

/* double pass cache buffer - (rarely) used in cases where alpha extraction
 * occurs for windowless plugins. */
 
bool
PluginInstanceChild::AlphaExtractCacheSetup()
{
    AlphaExtractCacheRelease();

    mAlphaExtract.hdc = ::CreateCompatibleDC(NULL);

    if (!mAlphaExtract.hdc)
        return false;

    BITMAPINFOHEADER bmih;
    memset((void*)&bmih, 0, sizeof(BITMAPINFOHEADER));
    bmih.biSize        = sizeof(BITMAPINFOHEADER);
    bmih.biWidth       = mWindow.width;
    bmih.biHeight      = mWindow.height;
    bmih.biPlanes      = 1;
    bmih.biBitCount    = 32;
    bmih.biCompression = BI_RGB;

    void* ppvBits = nsnull;
    mAlphaExtract.bmp = ::CreateDIBSection(mAlphaExtract.hdc,
                                           (BITMAPINFO*)&bmih,
                                           DIB_RGB_COLORS,
                                           (void**)&ppvBits,
                                           NULL,
                                           (unsigned long)sizeof(BITMAPINFOHEADER));
    if (!mAlphaExtract.bmp)
      return false;

    DeleteObject(::SelectObject(mAlphaExtract.hdc, mAlphaExtract.bmp));
    return true;
}

void
PluginInstanceChild::AlphaExtractCacheRelease()
{
    if (mAlphaExtract.bmp)
        ::DeleteObject(mAlphaExtract.bmp);

    if (mAlphaExtract.hdc)
        ::DeleteObject(mAlphaExtract.hdc);

    mAlphaExtract.bmp = NULL;
    mAlphaExtract.hdc = NULL;
}

void
PluginInstanceChild::UpdatePaintClipRect(RECT* aRect)
{
    if (aRect) {
        // Update the clip rect on our internal hdc
        HRGN clip = ::CreateRectRgnIndirect(aRect);
        ::SelectClipRgn(mSharedSurfaceDib.GetHDC(), clip);
        ::DeleteObject(clip);
    }
}

int16_t
PluginInstanceChild::SharedSurfacePaint(NPEvent& evcopy)
{
    if (!mPluginIface->event)
        return false;

    RECT* pRect = reinterpret_cast<RECT*>(evcopy.lParam);

    switch(mAlphaExtract.doublePass) {
        case RENDER_NATIVE:
            // pass the internal hdc to the plugin
            UpdatePaintClipRect(pRect);
            evcopy.wParam = WPARAM(mSharedSurfaceDib.GetHDC());
            return mPluginIface->event(&mData, reinterpret_cast<void*>(&evcopy));
        break;
        case RENDER_BACK_ONE:
              // Handle a double pass render used in alpha extraction for transparent
              // plugins. (See nsObjectFrame and gfxWindowsNativeDrawing for details.)
              // We render twice, once to the shared dib, and once to a cache which
              // we copy back on a second paint. These paints can't be spread across
              // multiple rpc messages as delays cause animation frame changes.
              if (!mAlphaExtract.bmp && !AlphaExtractCacheSetup()) {
                  mAlphaExtract.doublePass = RENDER_NATIVE;
                  return false;
              }

              // See gfxWindowsNativeDrawing, color order doesn't have to match.
              UpdatePaintClipRect(pRect);
              ::FillRect(mSharedSurfaceDib.GetHDC(), pRect, (HBRUSH)GetStockObject(WHITE_BRUSH));
              evcopy.wParam = WPARAM(mSharedSurfaceDib.GetHDC());
              if (!mPluginIface->event(&mData, reinterpret_cast<void*>(&evcopy))) {
                  mAlphaExtract.doublePass = RENDER_NATIVE;
                  return false;
              }

              // Copy to cache. We render to shared dib so we don't have to call
              // setwindow between calls (flash issue).  
              ::BitBlt(mAlphaExtract.hdc,
                       pRect->left,
                       pRect->top,
                       pRect->right - pRect->left,
                       pRect->bottom - pRect->top,
                       mSharedSurfaceDib.GetHDC(),
                       pRect->left,
                       pRect->top,
                       SRCCOPY);

              ::FillRect(mSharedSurfaceDib.GetHDC(), pRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
              if (!mPluginIface->event(&mData, reinterpret_cast<void*>(&evcopy))) {
                  mAlphaExtract.doublePass = RENDER_NATIVE;
                  return false;
              }
              mAlphaExtract.doublePass = RENDER_BACK_TWO;
              return true;
        break;
        case RENDER_BACK_TWO:
              // copy our cached surface back
              UpdatePaintClipRect(pRect);
              ::BitBlt(mSharedSurfaceDib.GetHDC(),
                       pRect->left,
                       pRect->top,
                       pRect->right - pRect->left,
                       pRect->bottom - pRect->top,
                       mAlphaExtract.hdc,
                       pRect->left,
                       pRect->top,
                       SRCCOPY);
              mAlphaExtract.doublePass = RENDER_NATIVE;
              return true;
        break;
    }
    return false;
}

/* flash msg throttling helpers */

// Flash has the unfortunate habit of flooding dispatch loops with custom
// windowing events they use for timing. We throttle these by dropping the
// delivery priority below any other event, including pending ipc io
// notifications. We do this for both windowed and windowless controls.
// Note flash's windowless msg window can last longer than our instance,
// so we try to unhook when the window is destroyed and in NPP_Destroy.

void
PluginInstanceChild::UnhookWinlessFlashThrottle()
{
  // We may have already unhooked
  if (!mWinlessThrottleOldWndProc)
      return;

  WNDPROC tmpProc = mWinlessThrottleOldWndProc;
  mWinlessThrottleOldWndProc = nsnull;

  NS_ASSERTION(mWinlessHiddenMsgHWND,
               "Missing mWinlessHiddenMsgHWND w/subclass set??");

  // reset the subclass
  SetWindowLongPtr(mWinlessHiddenMsgHWND, GWLP_WNDPROC,
                   reinterpret_cast<LONG_PTR>(tmpProc));

  // Remove our instance prop
  RemoveProp(mWinlessHiddenMsgHWND, kPluginInstanceChildProperty);
  mWinlessHiddenMsgHWND = nsnull;
}

// static
LRESULT CALLBACK
PluginInstanceChild::WinlessHiddenFlashWndProc(HWND hWnd,
                                               UINT message,
                                               WPARAM wParam,
                                               LPARAM lParam)
{
    PluginInstanceChild* self = reinterpret_cast<PluginInstanceChild*>(
        GetProp(hWnd, kPluginInstanceChildProperty));
    if (!self) {
        NS_NOTREACHED("Badness!");
        return 0;
    }

    NS_ASSERTION(self->mWinlessThrottleOldWndProc,
                 "Missing subclass procedure!!");

    // Throttle
    if (message == WM_USER+1) {
        self->FlashThrottleMessage(hWnd, message, wParam, lParam, false);
        return 0;
     }

    // Unhook
    if (message == WM_CLOSE || message == WM_NCDESTROY) {
        WNDPROC tmpProc = self->mWinlessThrottleOldWndProc;
        self->UnhookWinlessFlashThrottle();
        LRESULT res = CallWindowProc(tmpProc, hWnd, message, wParam, lParam);
        return res;
    }

    return CallWindowProc(self->mWinlessThrottleOldWndProc,
                          hWnd, message, wParam, lParam);
}

// Enumerate all thread windows looking for flash's hidden message window.
// Once we find it, sub class it so we can throttle user msgs.  
// static
BOOL CALLBACK
PluginInstanceChild::EnumThreadWindowsCallback(HWND hWnd,
                                               LPARAM aParam)
{
    PluginInstanceChild* self = reinterpret_cast<PluginInstanceChild*>(aParam);
    if (!self) {
        NS_NOTREACHED("Enum befuddled!");
        return FALSE;
    }

    PRUnichar className[64];
    if (!GetClassNameW(hWnd, className, sizeof(className)/sizeof(PRUnichar)))
      return TRUE;
    
    if (!wcscmp(className, L"SWFlash_PlaceholderX")) {
        WNDPROC oldWndProc =
            reinterpret_cast<WNDPROC>(GetWindowLongPtr(hWnd, GWLP_WNDPROC));
        // Only set this if we haven't already.
        if (oldWndProc != WinlessHiddenFlashWndProc) {
            if (self->mWinlessThrottleOldWndProc) {
                NS_WARNING("mWinlessThrottleWndProc already set???");
                return FALSE;
            }
            // Subsclass and store self as a property
            self->mWinlessHiddenMsgHWND = hWnd;
            self->mWinlessThrottleOldWndProc =
                reinterpret_cast<WNDPROC>(SetWindowLongPtr(hWnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(WinlessHiddenFlashWndProc)));
            SetProp(hWnd, kPluginInstanceChildProperty, self);
            NS_ASSERTION(self->mWinlessThrottleOldWndProc,
                         "SetWindowLongPtr failed?!");
        }
        // Return no matter what once we find the right window.
        return FALSE;
    }

    return TRUE;
}


void
PluginInstanceChild::SetupFlashMsgThrottle()
{
    if (mWindow.type == NPWindowTypeDrawable) {
        // Search for the flash hidden message window and subclass it. Only
        // search for flash windows belonging to our ui thread!
        if (mWinlessThrottleOldWndProc)
            return;
        EnumThreadWindows(GetCurrentThreadId(), EnumThreadWindowsCallback,
                          reinterpret_cast<LPARAM>(this));
    }
    else {
        // Already setup through quirks and the subclass.
        return;
    }
}

WNDPROC
PluginInstanceChild::FlashThrottleAsyncMsg::GetProc()
{ 
    if (mInstance) {
        return mWindowed ? mInstance->mPluginWndProc :
                           mInstance->mWinlessThrottleOldWndProc;
    }
    return nsnull;
}
 
void
PluginInstanceChild::FlashThrottleAsyncMsg::Run()
{
    RemoveFromAsyncList();

    // GetProc() checks mInstance, and pulls the procedure from
    // PluginInstanceChild. We don't transport sub-class procedure
    // ptrs around in FlashThrottleAsyncMsg msgs.
    if (!GetProc())
        return;
  
    // deliver the event to flash 
    CallWindowProc(GetProc(), GetWnd(), GetMsg(), GetWParam(), GetLParam());
}

void
PluginInstanceChild::FlashThrottleMessage(HWND aWnd,
                                          UINT aMsg,
                                          WPARAM aWParam,
                                          LPARAM aLParam,
                                          bool isWindowed)
{
    // We reuse ChildAsyncCall so we get the cancelation work
    // that's done in Destroy.
    FlashThrottleAsyncMsg* task = new FlashThrottleAsyncMsg(this,
        aWnd, aMsg, aWParam, aLParam, isWindowed);
    if (!task)
        return; 

    {
        MutexAutoLock lock(mAsyncCallMutex);
        mPendingAsyncCalls.AppendElement(task);
    }
    MessageLoop::current()->PostDelayedTask(FROM_HERE,
        task, kFlashWMUSERMessageThrottleDelayMs);
}

#endif // OS_WIN

bool
PluginInstanceChild::AnswerSetPluginFocus()
{
    PR_LOG(gPluginLog, PR_LOG_DEBUG, ("%s", FULLFUNCTION));

#if defined(OS_WIN)
    // Parent is letting us know something set focus to the plugin.
    if (::GetFocus() == mPluginWindowHWND)
        return true;
    ::SetFocus(mPluginWindowHWND);
    return true;
#else
    NS_NOTREACHED("PluginInstanceChild::AnswerSetPluginFocus not implemented!");
    return false;
#endif
}

bool
PluginInstanceChild::AnswerUpdateWindow()
{
    PR_LOG(gPluginLog, PR_LOG_DEBUG, ("%s", FULLFUNCTION));

#if defined(OS_WIN)
    if (mPluginWindowHWND) {
        RECT rect;
        if (GetUpdateRect(GetParent(mPluginWindowHWND), &rect, FALSE)) {
            ::InvalidateRect(mPluginWindowHWND, &rect, FALSE); 
        }
        UpdateWindow(mPluginWindowHWND);
    }
    return true;
#else
    NS_NOTREACHED("PluginInstanceChild::AnswerUpdateWindow not implemented!");
    return false;
#endif
}

PPluginScriptableObjectChild*
PluginInstanceChild::AllocPPluginScriptableObject()
{
    AssertPluginThread();
    return new PluginScriptableObjectChild(Proxy);
}

bool
PluginInstanceChild::DeallocPPluginScriptableObject(
    PPluginScriptableObjectChild* aObject)
{
    AssertPluginThread();
    delete aObject;
    return true;
}

bool
PluginInstanceChild::RecvPPluginScriptableObjectConstructor(
                                           PPluginScriptableObjectChild* aActor)
{
    AssertPluginThread();

    // This is only called in response to the parent process requesting the
    // creation of an actor. This actor will represent an NPObject that is
    // created by the browser and returned to the plugin.
    PluginScriptableObjectChild* actor =
        static_cast<PluginScriptableObjectChild*>(aActor);
    NS_ASSERTION(!actor->GetObject(false), "Actor already has an object?!");

    actor->InitializeProxy();
    NS_ASSERTION(actor->GetObject(false), "Actor should have an object!");

    return true;
}

bool
PluginInstanceChild::AnswerPBrowserStreamConstructor(
    PBrowserStreamChild* aActor,
    const nsCString& url,
    const uint32_t& length,
    const uint32_t& lastmodified,
    PStreamNotifyChild* notifyData,
    const nsCString& headers,
    const nsCString& mimeType,
    const bool& seekable,
    NPError* rv,
    uint16_t* stype)
{
    AssertPluginThread();
    *rv = static_cast<BrowserStreamChild*>(aActor)
          ->StreamConstructed(mimeType, seekable, stype);
    return true;
}

PBrowserStreamChild*
PluginInstanceChild::AllocPBrowserStream(const nsCString& url,
                                         const uint32_t& length,
                                         const uint32_t& lastmodified,
                                         PStreamNotifyChild* notifyData,
                                         const nsCString& headers,
                                         const nsCString& mimeType,
                                         const bool& seekable,
                                         NPError* rv,
                                         uint16_t *stype)
{
    AssertPluginThread();
    return new BrowserStreamChild(this, url, length, lastmodified,
                                  static_cast<StreamNotifyChild*>(notifyData),
                                  headers, mimeType, seekable, rv, stype);
}

bool
PluginInstanceChild::DeallocPBrowserStream(PBrowserStreamChild* stream)
{
    AssertPluginThread();
    delete stream;
    return true;
}

PPluginStreamChild*
PluginInstanceChild::AllocPPluginStream(const nsCString& mimeType,
                                        const nsCString& target,
                                        NPError* result)
{
    NS_RUNTIMEABORT("not callable");
    return NULL;
}

bool
PluginInstanceChild::DeallocPPluginStream(PPluginStreamChild* stream)
{
    AssertPluginThread();
    delete stream;
    return true;
}

PStreamNotifyChild*
PluginInstanceChild::AllocPStreamNotify(const nsCString& url,
                                        const nsCString& target,
                                        const bool& post,
                                        const nsCString& buffer,
                                        const bool& file,
                                        NPError* result)
{
    AssertPluginThread();
    NS_RUNTIMEABORT("not reached");
    return NULL;
}

void
StreamNotifyChild::ActorDestroy(ActorDestroyReason why)
{
    if (AncestorDeletion == why && mBrowserStream) {
        NS_ERROR("Pending NPP_URLNotify not called when closing an instance.");

        // reclaim responsibility for deleting ourself
        mBrowserStream->mStreamNotify = NULL;
        mBrowserStream = NULL;
    }
}


void
StreamNotifyChild::SetAssociatedStream(BrowserStreamChild* bs)
{
    NS_ASSERTION(bs, "Shouldn't be null");
    NS_ASSERTION(!mBrowserStream, "Two streams for one streamnotify?");

    mBrowserStream = bs;
}

bool
StreamNotifyChild::Recv__delete__(const NPReason& reason)
{
    AssertPluginThread();

    if (mBrowserStream)
        mBrowserStream->NotifyPending();
    else
        NPP_URLNotify(reason);

    return true;
}

void
StreamNotifyChild::NPP_URLNotify(NPReason reason)
{
    PluginInstanceChild* instance = static_cast<PluginInstanceChild*>(Manager());

    if (mClosure)
        instance->mPluginIface->urlnotify(instance->GetNPP(), mURL.get(),
                                          reason, mClosure);
}

bool
PluginInstanceChild::DeallocPStreamNotify(PStreamNotifyChild* notifyData)
{
    AssertPluginThread();

    if (!static_cast<StreamNotifyChild*>(notifyData)->mBrowserStream)
        delete notifyData;
    return true;
}

PluginScriptableObjectChild*
PluginInstanceChild::GetActorForNPObject(NPObject* aObject)
{
    AssertPluginThread();
    NS_ASSERTION(aObject, "Null pointer!");

    if (aObject->_class == PluginScriptableObjectChild::GetClass()) {
        // One of ours! It's a browser-provided object.
        ChildNPObject* object = static_cast<ChildNPObject*>(aObject);
        NS_ASSERTION(object->parent, "Null actor!");
        return object->parent;
    }

    PluginScriptableObjectChild* actor =
        PluginModuleChild::current()->GetActorForNPObject(aObject);
    if (actor) {
        // Plugin-provided object that we've previously wrapped.
        return actor;
    }

    actor = new PluginScriptableObjectChild(LocalObject);
    if (!SendPPluginScriptableObjectConstructor(actor)) {
        NS_ERROR("Failed to send constructor message!");
        return nsnull;
    }

    actor->InitializeLocal(aObject);
    return actor;
}

NPError
PluginInstanceChild::NPN_NewStream(NPMIMEType aMIMEType, const char* aWindow,
                                   NPStream** aStream)
{
    AssertPluginThread();

    PluginStreamChild* ps = new PluginStreamChild();

    NPError result;
    CallPPluginStreamConstructor(ps, nsDependentCString(aMIMEType),
                                 NullableString(aWindow), &result);
    if (NPERR_NO_ERROR != result) {
        *aStream = NULL;
        PPluginStreamChild::Call__delete__(ps, NPERR_GENERIC_ERROR, true);
        return result;
    }

    *aStream = &ps->mStream;
    return NPERR_NO_ERROR;
}

void
PluginInstanceChild::InvalidateRect(NPRect* aInvalidRect)
{
    NS_ASSERTION(aInvalidRect, "Null pointer!");

#ifdef OS_WIN
    // Invalidate and draw locally for windowed plugins.
    if (mWindow.type == NPWindowTypeWindow) {
      NS_ASSERTION(IsWindow(mPluginWindowHWND), "Bad window?!");
      RECT rect = { aInvalidRect->left, aInvalidRect->top,
                    aInvalidRect->right, aInvalidRect->bottom };
      ::InvalidateRect(mPluginWindowHWND, &rect, FALSE);
      return;
    }
#endif

    SendNPN_InvalidateRect(*aInvalidRect);
}

uint32_t
PluginInstanceChild::ScheduleTimer(uint32_t interval, bool repeat,
                                   TimerFunc func)
{
    ChildTimer* t = new ChildTimer(this, interval, repeat, func);
    if (0 == t->ID()) {
        delete t;
        return 0;
    }

    mTimers.AppendElement(t);
    return t->ID();
}

void
PluginInstanceChild::UnscheduleTimer(uint32_t id)
{
    if (0 == id)
        return;

    mTimers.RemoveElement(id, ChildTimer::IDComparator());
}

void
PluginInstanceChild::AsyncCall(PluginThreadCallback aFunc, void* aUserData)
{
    ChildAsyncCall* task = new ChildAsyncCall(this, aFunc, aUserData);

    {
        MutexAutoLock lock(mAsyncCallMutex);
        mPendingAsyncCalls.AppendElement(task);
    }
    ProcessChild::message_loop()->PostTask(FROM_HERE, task);
}

static PLDHashOperator
InvalidateObject(DeletingObjectEntry* e, void* userArg)
{
    NPObject* o = e->GetKey();
    if (!e->mDeleted && o->_class && o->_class->invalidate)
        o->_class->invalidate(o);

    return PL_DHASH_NEXT;
}

static PLDHashOperator
DeleteObject(DeletingObjectEntry* e, void* userArg)
{
    NPObject* o = e->GetKey();
    if (!e->mDeleted) {
        e->mDeleted = true;

#ifdef NS_BUILD_REFCNT_LOGGING
        {
            int32_t refcnt = o->referenceCount;
            while (refcnt) {
                --refcnt;
                NS_LOG_RELEASE(o, refcnt, "NPObject");
            }
        }
#endif

        PluginModuleChild::DeallocNPObject(o);
    }

    return PL_DHASH_NEXT;
}

bool
PluginInstanceChild::AnswerNPP_Destroy(NPError* aResult)
{
    PLUGIN_LOG_DEBUG_METHOD;
    AssertPluginThread();

    nsTArray<PBrowserStreamChild*> streams;
    ManagedPBrowserStreamChild(streams);

    // First make sure none of these streams become deleted
    for (PRUint32 i = 0; i < streams.Length(); ) {
        if (static_cast<BrowserStreamChild*>(streams[i])->InstanceDying())
            ++i;
        else
            streams.RemoveElementAt(i);
    }
    for (PRUint32 i = 0; i < streams.Length(); ++i)
        static_cast<BrowserStreamChild*>(streams[i])->FinishDelivery();

    {
        MutexAutoLock lock(mAsyncCallMutex);
        for (PRUint32 i = 0; i < mPendingAsyncCalls.Length(); ++i)
            mPendingAsyncCalls[i]->Cancel();
        mPendingAsyncCalls.TruncateLength(0);
    }

    mTimers.Clear();

    PluginModuleChild::current()->NPP_Destroy(this);
    mData.ndata = 0;

    mDeletingHash = new nsTHashtable<DeletingObjectEntry>;
    mDeletingHash->Init();
    PluginModuleChild::current()->FindNPObjectsForInstance(this);

    mDeletingHash->EnumerateEntries(InvalidateObject, NULL);
    mDeletingHash->EnumerateEntries(DeleteObject, NULL);

    // Null out our cached actors as they should have been killed in the
    // PluginInstanceDestroyed call above.
    mCachedWindowActor = nsnull;
    mCachedElementActor = nsnull;

#if defined(OS_WIN)
    SharedSurfaceRelease();
    DestroyWinlessPopupSurrogate();
    UnhookWinlessFlashThrottle();
#endif

    return true;
}
