/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
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

#ifndef DOM_PLUGINS_PLUGINMESSAGEUTILS_H
#define DOM_PLUGINS_PLUGINMESSAGEUTILS_H

#include "IPC/IPCMessageUtils.h"
#include "base/message_loop.h"

#include "mozilla/ipc/RPCChannel.h"

#include "npapi.h"
#include "npruntime.h"
#include "npfunctions.h"
#include "nsAutoPtr.h"
#include "nsStringGlue.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "prlog.h"
#include "nsHashKeys.h"
#ifdef MOZ_CRASHREPORTER
#  include "nsExceptionHandler.h"
#endif

namespace mozilla {
namespace plugins {

enum ScriptableObjectType
{
  LocalObject,
  Proxy
};

mozilla::ipc::RPCChannel::RacyRPCPolicy
MediateRace(const mozilla::ipc::RPCChannel::Message& parent,
            const mozilla::ipc::RPCChannel::Message& child);

std::string
MungePluginDsoPath(const std::string& path);
std::string
UnmungePluginDsoPath(const std::string& munged);

extern PRLogModuleInfo* gPluginLog;

#if defined(_MSC_VER)
#define FULLFUNCTION __FUNCSIG__
#elif (__GNUC__ >= 4)
#define FULLFUNCTION __PRETTY_FUNCTION__
#else
#define FULLFUNCTION __FUNCTION__
#endif

#define PLUGIN_LOG_DEBUG(args) PR_LOG(gPluginLog, PR_LOG_DEBUG, args)
#define PLUGIN_LOG_DEBUG_FUNCTION PR_LOG(gPluginLog, PR_LOG_DEBUG, ("%s", FULLFUNCTION))
#define PLUGIN_LOG_DEBUG_METHOD PR_LOG(gPluginLog, PR_LOG_DEBUG, ("%s [%p]", FULLFUNCTION, (void*) this))

/**
 * This is NPByteRange without the linked list.
 */
struct IPCByteRange
{
  int32_t offset;
  uint32_t length;
};  

typedef std::vector<IPCByteRange> IPCByteRanges;

typedef nsCString Buffer;

struct NPRemoteWindow
{
  unsigned long window;
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
  NPRect clipRect;
  NPWindowType type;
#if defined(MOZ_X11) && defined(XP_UNIX) && !defined(XP_MACOSX)
  VisualID visualID;
  Colormap colormap;
#endif /* XP_UNIX */
#if defined(XP_WIN)
  base::SharedMemoryHandle surfaceHandle;
#endif
};

#ifdef XP_WIN
typedef HWND NativeWindowHandle;
#elif defined(MOZ_X11)
typedef XID NativeWindowHandle;
#elif defined(XP_MACOSX) || defined(ANDROID)
typedef intptr_t NativeWindowHandle; // never actually used, will always be 0
#else
#error Need NativeWindowHandle for this platform
#endif

#ifdef MOZ_CRASHREPORTER
typedef CrashReporter::ThreadId NativeThreadId;
#else
// unused in this case
typedef int32 NativeThreadId;
#endif

// XXX maybe not the best place for these. better one?

#define VARSTR(v_)  case v_: return #v_
inline const char* const
NPPVariableToString(NPPVariable aVar)
{
    switch (aVar) {
        VARSTR(NPPVpluginNameString);
        VARSTR(NPPVpluginDescriptionString);
        VARSTR(NPPVpluginWindowBool);
        VARSTR(NPPVpluginTransparentBool);
        VARSTR(NPPVjavaClass);
        VARSTR(NPPVpluginWindowSize);
        VARSTR(NPPVpluginTimerInterval);

        VARSTR(NPPVpluginScriptableInstance);
        VARSTR(NPPVpluginScriptableIID);

        VARSTR(NPPVjavascriptPushCallerBool);

        VARSTR(NPPVpluginKeepLibraryInMemory);
        VARSTR(NPPVpluginNeedsXEmbed);

        VARSTR(NPPVpluginScriptableNPObject);

        VARSTR(NPPVformValue);
  
        VARSTR(NPPVpluginUrlRequestsDisplayedBool);
  
        VARSTR(NPPVpluginWantsAllNetworkStreams);

#ifdef XP_MACOSX
        VARSTR(NPPVpluginDrawingModel);
        VARSTR(NPPVpluginEventModel);
#endif

    default: return "???";
    }
}

inline const char*
NPNVariableToString(NPNVariable aVar)
{
    switch(aVar) {
        VARSTR(NPNVxDisplay);
        VARSTR(NPNVxtAppContext);
        VARSTR(NPNVnetscapeWindow);
        VARSTR(NPNVjavascriptEnabledBool);
        VARSTR(NPNVasdEnabledBool);
        VARSTR(NPNVisOfflineBool);

        VARSTR(NPNVserviceManager);
        VARSTR(NPNVDOMElement);
        VARSTR(NPNVDOMWindow);
        VARSTR(NPNVToolkit);
        VARSTR(NPNVSupportsXEmbedBool);

        VARSTR(NPNVWindowNPObject);

        VARSTR(NPNVPluginElementNPObject);

        VARSTR(NPNVSupportsWindowless);

        VARSTR(NPNVprivateModeBool);

    default: return "???";
    }
}
#undef VARSTR

inline bool IsPluginThread()
{
  MessageLoop* loop = MessageLoop::current();
  if (!loop)
      return false;
  return (loop->type() == MessageLoop::TYPE_UI);
}

inline void AssertPluginThread()
{
  NS_ASSERTION(IsPluginThread(), "Should be on the plugin's main thread!");
}

#define ENSURE_PLUGIN_THREAD(retval) \
  PR_BEGIN_MACRO \
    if (!IsPluginThread()) { \
      NS_WARNING("Not running on the plugin's main thread!"); \
      return (retval); \
    } \
  PR_END_MACRO

#define ENSURE_PLUGIN_THREAD_VOID() \
  PR_BEGIN_MACRO \
    if (!IsPluginThread()) { \
      NS_WARNING("Not running on the plugin's main thread!"); \
      return; \
    } \
  PR_END_MACRO

void DeferNPObjectLastRelease(const NPNetscapeFuncs* f, NPObject* o);
void DeferNPVariantLastRelease(const NPNetscapeFuncs* f, NPVariant* v);

// in NPAPI, char* == NULL is sometimes meaningful.  the following is
// helper code for dealing with nullable nsCString's
inline nsCString
NullableString(const char* aString)
{
    if (!aString) {
        nsCString str;
        str.SetIsVoid(PR_TRUE);
        return str;
    }
    return nsCString(aString);
}

inline const char*
NullableStringGet(const nsCString& str)
{
  if (str.IsVoid())
    return NULL;

  return str.get();
}

struct DeletingObjectEntry : public nsPtrHashKey<NPObject>
{
  DeletingObjectEntry(const NPObject* key)
    : nsPtrHashKey<NPObject>(key)
    , mDeleted(false)
  { }

  bool mDeleted;
};

#ifdef XP_WIN
// The private event used for double-pass widgetless plugin rendering.
UINT DoublePassRenderingEvent();
#endif

} /* namespace plugins */

} /* namespace mozilla */

namespace IPC {

template <>
struct ParamTraits<NPRect>
{
  typedef NPRect paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.top);
    WriteParam(aMsg, aParam.left);
    WriteParam(aMsg, aParam.bottom);
    WriteParam(aMsg, aParam.right);
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    uint16_t top, left, bottom, right;
    if (ReadParam(aMsg, aIter, &top) &&
        ReadParam(aMsg, aIter, &left) &&
        ReadParam(aMsg, aIter, &bottom) &&
        ReadParam(aMsg, aIter, &right)) {
      aResult->top = top;
      aResult->left = left;
      aResult->bottom = bottom;
      aResult->right = right;
      return true;
    }
    return false;
  }

  static void Log(const paramType& aParam, std::wstring* aLog)
  {
    aLog->append(StringPrintf(L"[%u, %u, %u, %u]", aParam.top, aParam.left,
                              aParam.bottom, aParam.right));
  }
};

template <>
struct ParamTraits<NPWindowType>
{
  typedef NPWindowType paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    aMsg->WriteInt16(int16(aParam));
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    int16 result;
    if (aMsg->ReadInt16(aIter, &result)) {
      *aResult = paramType(result);
      return true;
    }
    return false;
  }

  static void Log(const paramType& aParam, std::wstring* aLog)
  {
    aLog->append(StringPrintf(L"%d", int16(aParam)));
  }
};

template <>
struct ParamTraits<mozilla::plugins::NPRemoteWindow>
{
  typedef mozilla::plugins::NPRemoteWindow paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    aMsg->WriteULong(aParam.window);
    WriteParam(aMsg, aParam.x);
    WriteParam(aMsg, aParam.y);
    WriteParam(aMsg, aParam.width);
    WriteParam(aMsg, aParam.height);
    WriteParam(aMsg, aParam.clipRect);
    WriteParam(aMsg, aParam.type);
#if defined(MOZ_X11) && defined(XP_UNIX) && !defined(XP_MACOSX)
    aMsg->WriteULong(aParam.visualID);
    aMsg->WriteULong(aParam.colormap);
#endif
#if defined(XP_WIN)
    WriteParam(aMsg, aParam.surfaceHandle);
#endif
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    unsigned long window;
    int32_t x, y;
    uint32_t width, height;
    NPRect clipRect;
    NPWindowType type;
    if (!(aMsg->ReadULong(aIter, &window) &&
          ReadParam(aMsg, aIter, &x) &&
          ReadParam(aMsg, aIter, &y) &&
          ReadParam(aMsg, aIter, &width) &&
          ReadParam(aMsg, aIter, &height) &&
          ReadParam(aMsg, aIter, &clipRect) &&
          ReadParam(aMsg, aIter, &type)))
      return false;

#if defined(MOZ_X11) && defined(XP_UNIX) && !defined(XP_MACOSX)
    unsigned long visualID;
    unsigned long colormap;
    if (!(aMsg->ReadULong(aIter, &visualID) &&
          aMsg->ReadULong(aIter, &colormap)))
      return false;
#endif

#if defined(XP_WIN)
    base::SharedMemoryHandle surfaceHandle;
    if (!ReadParam(aMsg, aIter, &surfaceHandle))
      return false;
#endif

    aResult->window = window;
    aResult->x = x;
    aResult->y = y;
    aResult->width = width;
    aResult->height = height;
    aResult->clipRect = clipRect;
    aResult->type = type;
#if defined(MOZ_X11) && defined(XP_UNIX) && !defined(XP_MACOSX)
    aResult->visualID = visualID;
    aResult->colormap = colormap;
#endif
#if defined(XP_WIN)
    aResult->surfaceHandle = surfaceHandle;
#endif
    return true;
  }

  static void Log(const paramType& aParam, std::wstring* aLog)
  {
    aLog->append(StringPrintf(L"[%u, %d, %d, %u, %u, %d",
                              (unsigned long)aParam.window,
                              aParam.x, aParam.y, aParam.width,
                              aParam.height, (long)aParam.type));
  }
};

template <>
struct ParamTraits<NPString>
{
  typedef NPString paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.UTF8Length);
    aMsg->WriteBytes(aParam.UTF8Characters,
                     aParam.UTF8Length * sizeof(NPUTF8));
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    if (ReadParam(aMsg, aIter, &aResult->UTF8Length)) {
      int byteCount = aResult->UTF8Length * sizeof(NPUTF8);
      if (!byteCount) {
        aResult->UTF8Characters = "\0";
        return true;
      }

      const char* messageBuffer = nsnull;
      nsAutoArrayPtr<char> newBuffer(new char[byteCount]);
      if (newBuffer && aMsg->ReadBytes(aIter, &messageBuffer, byteCount )) {
        memcpy((void*)messageBuffer, newBuffer.get(), byteCount);
        aResult->UTF8Characters = newBuffer.forget();
        return true;
      }
    }
    return false;
  }

  static void Log(const paramType& aParam, std::wstring* aLog)
  {
    aLog->append(StringPrintf(L"%s", aParam.UTF8Characters));
  }
};

#ifdef XP_MACOSX
template <>
struct ParamTraits<NPNSString*>
{
  typedef NPNSString* paramType;

  // Empty string writes a length of 0 and no buffer.
  // We don't write a NULL terminating character in buffers.
  static void Write(Message* aMsg, const paramType& aParam)
  {
    CFStringRef cfString = (CFStringRef)aParam;

    // Write true if we have a string, false represents NULL.
    aMsg->WriteBool(!!cfString);
    if (!cfString) {
      return;
    }

    long length = ::CFStringGetLength(cfString);
    WriteParam(aMsg, length);
    if (length == 0) {
      return;
    }

    // Attempt to get characters without any allocation/conversion.
    if (::CFStringGetCharactersPtr(cfString)) {
      aMsg->WriteBytes(::CFStringGetCharactersPtr(cfString), length * sizeof(UniChar));
    } else {
      UniChar *buffer = (UniChar*)moz_xmalloc(length * sizeof(UniChar));
      ::CFStringGetCharacters(cfString, ::CFRangeMake(0, length), buffer);
      aMsg->WriteBytes(buffer, length * sizeof(UniChar));
      free(buffer);
    }
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    bool haveString = false;
    if (!aMsg->ReadBool(aIter, &haveString)) {
      return false;
    }
    if (!haveString) {
      *aResult = NULL;
      return true;
    }

    long length;
    if (!ReadParam(aMsg, aIter, &length)) {
      return false;
    }

    UniChar* buffer = nsnull;
    if (length != 0) {
      if (!aMsg->ReadBytes(aIter, (const char**)&buffer, length * sizeof(UniChar)) ||
          !buffer) {
        return false;
      }
    }

    *aResult = (NPNSString*)::CFStringCreateWithBytes(kCFAllocatorDefault, (UInt8*)buffer,
                                                      length * sizeof(UniChar),
                                                      kCFStringEncodingUTF16, false);
    if (!*aResult) {
      return false;
    }

    return true;
  }
};
#endif

template <>
struct ParamTraits<NPVariant>
{
  typedef NPVariant paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    if (NPVARIANT_IS_VOID(aParam)) {
      aMsg->WriteInt(0);
      return;
    }

    if (NPVARIANT_IS_NULL(aParam)) {
      aMsg->WriteInt(1);
      return;
    }

    if (NPVARIANT_IS_BOOLEAN(aParam)) {
      aMsg->WriteInt(2);
      WriteParam(aMsg, NPVARIANT_TO_BOOLEAN(aParam));
      return;
    }

    if (NPVARIANT_IS_INT32(aParam)) {
      aMsg->WriteInt(3);
      WriteParam(aMsg, NPVARIANT_TO_INT32(aParam));
      return;
    }

    if (NPVARIANT_IS_DOUBLE(aParam)) {
      aMsg->WriteInt(4);
      WriteParam(aMsg, NPVARIANT_TO_DOUBLE(aParam));
      return;
    }

    if (NPVARIANT_IS_STRING(aParam)) {
      aMsg->WriteInt(5);
      WriteParam(aMsg, NPVARIANT_TO_STRING(aParam));
      return;
    }

    NS_ERROR("Unsupported type!");
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    int type;
    if (!aMsg->ReadInt(aIter, &type)) {
      return false;
    }

    switch (type) {
      case 0:
        VOID_TO_NPVARIANT(*aResult);
        return true;

      case 1:
        NULL_TO_NPVARIANT(*aResult);
        return true;

      case 2: {
        bool value;
        if (ReadParam(aMsg, aIter, &value)) {
          BOOLEAN_TO_NPVARIANT(value, *aResult);
          return true;
        }
      } break;

      case 3: {
        int32 value;
        if (ReadParam(aMsg, aIter, &value)) {
          INT32_TO_NPVARIANT(value, *aResult);
          return true;
        }
      } break;

      case 4: {
        double value;
        if (ReadParam(aMsg, aIter, &value)) {
          DOUBLE_TO_NPVARIANT(value, *aResult);
          return true;
        }
      } break;

      case 5: {
        NPString value;
        if (ReadParam(aMsg, aIter, &value)) {
          STRINGN_TO_NPVARIANT(value.UTF8Characters, value.UTF8Length,
                               *aResult);
          return true;
        }
      } break;

      default:
        NS_ERROR("Unsupported type!");
    }

    return false;
  }

  static void Log(const paramType& aParam, std::wstring* aLog)
  {
    if (NPVARIANT_IS_VOID(aParam)) {
      aLog->append(L"[void]");
      return;
    }

    if (NPVARIANT_IS_NULL(aParam)) {
      aLog->append(L"[null]");
      return;
    }

    if (NPVARIANT_IS_BOOLEAN(aParam)) {
      LogParam(NPVARIANT_TO_BOOLEAN(aParam), aLog);
      return;
    }

    if (NPVARIANT_IS_INT32(aParam)) {
      LogParam(NPVARIANT_TO_INT32(aParam), aLog);
      return;
    }

    if (NPVARIANT_IS_DOUBLE(aParam)) {
      LogParam(NPVARIANT_TO_DOUBLE(aParam), aLog);
      return;
    }

    if (NPVARIANT_IS_STRING(aParam)) {
      LogParam(NPVARIANT_TO_STRING(aParam), aLog);
      return;
    }

    NS_ERROR("Unsupported type!");
  }
};

template <>
struct ParamTraits<mozilla::plugins::IPCByteRange>
{
  typedef mozilla::plugins::IPCByteRange paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.offset);
    WriteParam(aMsg, aParam.length);
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    paramType p;
    if (ReadParam(aMsg, aIter, &p.offset) &&
        ReadParam(aMsg, aIter, &p.length)) {
      *aResult = p;
      return true;
    }
    return false;
  }
};

template <>
struct ParamTraits<NPNVariable>
{
  typedef NPNVariable paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, int(aParam));
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    int intval;
    if (ReadParam(aMsg, aIter, &intval)) {
      *aResult = paramType(intval);
      return true;
    }
    return false;
  }
};

template<>
struct ParamTraits<NPNURLVariable>
{
  typedef NPNURLVariable paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, int(aParam));
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    int intval;
    if (ReadParam(aMsg, aIter, &intval)) {
      switch (intval) {
      case NPNURLVCookie:
      case NPNURLVProxy:
        *aResult = paramType(intval);
        return true;
      }
    }
    return false;
  }
};

  
template<>
struct ParamTraits<NPCoordinateSpace>
{
  typedef NPCoordinateSpace paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, int32(aParam));
  }

  static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
  {
    int32 intval;
    if (ReadParam(aMsg, aIter, &intval)) {
      switch (intval) {
      case NPCoordinateSpacePlugin:
      case NPCoordinateSpaceWindow:
      case NPCoordinateSpaceFlippedWindow:
      case NPCoordinateSpaceScreen:
      case NPCoordinateSpaceFlippedScreen:
        *aResult = paramType(intval);
        return true;
      }
    }
    return false;
  }
};

} /* namespace IPC */


// Serializing NPEvents is completely platform-specific and can be rather
// intricate depending on the platform.  So for readability we split it
// into separate files and have the only macro crud live here.
// 
// NB: these guards are based on those where struct NPEvent is defined
// in npapi.h.  They should be kept in sync.
#if defined(XP_MACOSX)
#  include "mozilla/plugins/NPEventOSX.h"
#elif defined(XP_WIN)
#  include "mozilla/plugins/NPEventWindows.h"
#elif defined(XP_OS2)
#  error Sorry, OS/2 is not supported
#elif defined(XP_UNIX) && defined(MOZ_X11)
#  include "mozilla/plugins/NPEventX11.h"
#elif defined(ANDROID)
#  include "mozilla/plugins/NPEventAndroid.h"
#else
#  error Unsupported platform
#endif

#endif /* DOM_PLUGINS_PLUGINMESSAGEUTILS_H */
