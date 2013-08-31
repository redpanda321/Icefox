/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef dom_plugins_PluginModuleParent_h
#define dom_plugins_PluginModuleParent_h 1

#include <cstring>

#include "base/basictypes.h"

#include "prlink.h"

#include "npapi.h"
#include "npfunctions.h"

#include "base/string_util.h"

#include "mozilla/FileUtils.h"
#include "mozilla/PluginLibrary.h"
#include "mozilla/plugins/PPluginModuleParent.h"
#include "mozilla/plugins/PluginInstanceParent.h"
#include "mozilla/plugins/PluginProcessParent.h"
#include "mozilla/plugins/PluginIdentifierParent.h"

#include "nsAutoPtr.h"
#include "nsDataHashtable.h"
#include "nsHashKeys.h"
#include "nsIFileStreams.h"

#ifdef MOZ_CRASHREPORTER
#include "nsExceptionHandler.h"
#endif

namespace mozilla {
namespace dom {
class PCrashReporterParent;
class CrashReporterParent;
}

namespace plugins {
//-----------------------------------------------------------------------------

class BrowserStreamParent;

/**
 * PluginModuleParent
 *
 * This class implements the NPP API from the perspective of the rest
 * of Gecko, forwarding NPP calls along to the child process that is
 * actually running the plugin.
 *
 * This class /also/ implements a version of the NPN API, because the
 * child process needs to make these calls back into Gecko proper.
 * This class is responsible for "actually" making those function calls.
 */
class PluginModuleParent
    : public PPluginModuleParent
    , public PluginLibrary
#ifdef MOZ_CRASHREPORTER_INJECTOR
    , public CrashReporter::InjectorCrashCallback
#endif
{
private:
    typedef mozilla::PluginLibrary PluginLibrary;
    typedef mozilla::dom::PCrashReporterParent PCrashReporterParent;
    typedef mozilla::dom::CrashReporterParent CrashReporterParent;

protected:

    virtual PPluginIdentifierParent*
    AllocPPluginIdentifier(const nsCString& aString,
                           const int32_t& aInt,
                           const bool& aTemporary);

    virtual bool
    DeallocPPluginIdentifier(PPluginIdentifierParent* aActor);

    PPluginInstanceParent*
    AllocPPluginInstance(const nsCString& aMimeType,
                         const uint16_t& aMode,
                         const InfallibleTArray<nsCString>& aNames,
                         const InfallibleTArray<nsCString>& aValues,
                         NPError* rv);

    virtual bool
    DeallocPPluginInstance(PPluginInstanceParent* aActor);

public:
    // aFilePath is UTF8, not native!
    PluginModuleParent(const char* aFilePath);
    virtual ~PluginModuleParent();

    virtual void SetPlugin(nsNPAPIPlugin* plugin) MOZ_OVERRIDE
    {
        mPlugin = plugin;
    }

    virtual void ActorDestroy(ActorDestroyReason why) MOZ_OVERRIDE;

    /**
     * LoadModule
     *
     * This may or may not launch a plugin child process,
     * and may or may not be very expensive.
     */
    static PluginLibrary* LoadModule(const char* aFilePath);

    const NPNetscapeFuncs* GetNetscapeFuncs() {
        return mNPNIface;
    }

    PluginProcessParent* Process() const { return mSubprocess; }
    base::ProcessHandle ChildProcessHandle() { return mSubprocess->GetChildProcessHandle(); }

    bool OkToCleanup() const {
        return !IsOnCxxStack();
    }

    /**
     * Get an identifier actor for this NPIdentifier. If this is a temporary
     * identifier, the temporary refcount is increased by one. This method
     * is intended only for use by StackIdentifier and the scriptable
     * Enumerate hook.
     */
    PluginIdentifierParent*
    GetIdentifierForNPIdentifier(NPP npp, NPIdentifier aIdentifier);

    void ProcessRemoteNativeEventsInRPCCall();

protected:
    virtual mozilla::ipc::RPCChannel::RacyRPCPolicy
    MediateRPCRace(const Message& parent, const Message& child) MOZ_OVERRIDE
    {
        return MediateRace(parent, child);
    }

    virtual bool RecvXXX_HACK_FIXME_cjones(Shmem& mem) { NS_RUNTIMEABORT("not reached"); return false; }

    virtual bool ShouldContinueFromReplyTimeout() MOZ_OVERRIDE;

    virtual bool
    RecvBackUpXResources(const FileDescriptor& aXSocketFd) MOZ_OVERRIDE;

    virtual bool
    AnswerNPN_UserAgent(nsCString* userAgent);

    virtual bool
    AnswerNPN_GetValue_WithBoolReturn(const NPNVariable& aVariable,
                                      NPError* aError,
                                      bool* aBoolVal);

    virtual bool AnswerProcessSomeEvents() MOZ_OVERRIDE;

    virtual bool
    RecvProcessNativeEventsInRPCCall() MOZ_OVERRIDE;

    virtual bool
    RecvPluginShowWindow(const uint32_t& aWindowId, const bool& aModal,
                         const int32_t& aX, const int32_t& aY,
                         const size_t& aWidth, const size_t& aHeight) MOZ_OVERRIDE;

    virtual bool
    RecvPluginHideWindow(const uint32_t& aWindowId) MOZ_OVERRIDE;

    virtual PCrashReporterParent*
    AllocPCrashReporter(mozilla::dom::NativeThreadId* id,
                        uint32_t* processType) MOZ_OVERRIDE;
    virtual bool
    DeallocPCrashReporter(PCrashReporterParent* actor) MOZ_OVERRIDE;

    virtual bool
    RecvSetCursor(const NSCursorInfo& aCursorInfo) MOZ_OVERRIDE;

    virtual bool
    RecvShowCursor(const bool& aShow) MOZ_OVERRIDE;

    virtual bool
    RecvPushCursor(const NSCursorInfo& aCursorInfo) MOZ_OVERRIDE;

    virtual bool
    RecvPopCursor() MOZ_OVERRIDE;

    virtual bool
    RecvGetNativeCursorsSupported(bool* supported) MOZ_OVERRIDE;

    virtual bool
    RecvNPN_SetException(PPluginScriptableObjectParent* aActor,
                         const nsCString& aMessage) MOZ_OVERRIDE;

    virtual bool
    RecvNPN_ReloadPlugins(const bool& aReloadPages) MOZ_OVERRIDE;

    static PluginInstanceParent* InstCast(NPP instance);
    static BrowserStreamParent* StreamCast(NPP instance, NPStream* s);

private:
    void SetPluginFuncs(NPPluginFuncs* aFuncs);

    // Implement the module-level functions from NPAPI; these are
    // normally resolved directly from the DSO.
#ifdef OS_LINUX
    NPError NP_Initialize(const NPNetscapeFuncs* npnIface,
                          NPPluginFuncs* nppIface);
#else
    NPError NP_Initialize(const NPNetscapeFuncs* npnIface);
    NPError NP_GetEntryPoints(NPPluginFuncs* nppIface);
#endif

    // NPP-like API that Gecko calls are trampolined into.  These 
    // messages then get forwarded along to the plugin instance,
    // and then eventually the child process.

    static NPError NPP_Destroy(NPP instance, NPSavedData** save);

    static NPError NPP_SetWindow(NPP instance, NPWindow* window);
    static NPError NPP_NewStream(NPP instance, NPMIMEType type, NPStream* stream,
                                 NPBool seekable, uint16_t* stype);
    static NPError NPP_DestroyStream(NPP instance,
                                     NPStream* stream, NPReason reason);
    static int32_t NPP_WriteReady(NPP instance, NPStream* stream);
    static int32_t NPP_Write(NPP instance, NPStream* stream,
                             int32_t offset, int32_t len, void* buffer);
    static void NPP_StreamAsFile(NPP instance,
                                 NPStream* stream, const char* fname);
    static void NPP_Print(NPP instance, NPPrint* platformPrint);
    static int16_t NPP_HandleEvent(NPP instance, void* event);
    static void NPP_URLNotify(NPP instance, const char* url,
                              NPReason reason, void* notifyData);
    static NPError NPP_GetValue(NPP instance,
                                NPPVariable variable, void *ret_value);
    static NPError NPP_SetValue(NPP instance, NPNVariable variable,
                                void *value);
    static void NPP_URLRedirectNotify(NPP instance, const char* url,
                                      int32_t status, void* notifyData);

    virtual bool HasRequiredFunctions();
    virtual nsresult AsyncSetWindow(NPP instance, NPWindow* window);
    virtual nsresult GetImageContainer(NPP instance, mozilla::layers::ImageContainer** aContainer);
    virtual nsresult GetImageSize(NPP instance, nsIntSize* aSize);
    virtual bool IsOOP() MOZ_OVERRIDE { return true; }
    virtual nsresult SetBackgroundUnknown(NPP instance) MOZ_OVERRIDE;
    virtual nsresult BeginUpdateBackground(NPP instance,
                                           const nsIntRect& aRect,
                                           gfxContext** aCtx) MOZ_OVERRIDE;
    virtual nsresult EndUpdateBackground(NPP instance,
                                         gfxContext* aCtx,
                                         const nsIntRect& aRect) MOZ_OVERRIDE;

#if defined(XP_UNIX) && !defined(XP_MACOSX) && !defined(MOZ_WIDGET_GONK)
    virtual nsresult NP_Initialize(NPNetscapeFuncs* bFuncs, NPPluginFuncs* pFuncs, NPError* error);
#else
    virtual nsresult NP_Initialize(NPNetscapeFuncs* bFuncs, NPError* error);
#endif
    virtual nsresult NP_Shutdown(NPError* error);
    virtual nsresult NP_GetMIMEDescription(const char** mimeDesc);
    virtual nsresult NP_GetValue(void *future, NPPVariable aVariable,
                                 void *aValue, NPError* error);
#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
    virtual nsresult NP_GetEntryPoints(NPPluginFuncs* pFuncs, NPError* error);
#endif
    virtual nsresult NPP_New(NPMIMEType pluginType, NPP instance,
                             uint16_t mode, int16_t argc, char* argn[],
                             char* argv[], NPSavedData* saved,
                             NPError* error);
    virtual nsresult NPP_ClearSiteData(const char* site, uint64_t flags,
                                       uint64_t maxAge);
    virtual nsresult NPP_GetSitesWithData(InfallibleTArray<nsCString>& result);

#if defined(XP_MACOSX)
    virtual nsresult IsRemoteDrawingCoreAnimation(NPP instance, bool *aDrawing);
    virtual nsresult ContentsScaleFactorChanged(NPP instance, double aContentsScaleFactor);
#endif
#if defined(MOZ_WIDGET_QT) && (MOZ_PLATFORM_MAEMO == 6)
    virtual nsresult HandleGUIEvent(NPP instance, const nsGUIEvent& anEvent,
                                    bool* handled);
#endif

private:
    CrashReporterParent* CrashReporter();

#ifdef MOZ_CRASHREPORTER
    void ProcessFirstMinidump();
    void WriteExtraDataForMinidump(CrashReporter::AnnotationTable& notes);
#endif
    void CleanupFromTimeout();
    static int TimeoutChanged(const char* aPref, void* aModule);
    void NotifyPluginCrashed();

    PluginProcessParent* mSubprocess;
    // the plugin thread in mSubprocess
    NativeThreadId mPluginThread;
    bool mShutdown;
    bool mClearSiteDataSupported;
    bool mGetSitesWithDataSupported;
    const NPNetscapeFuncs* mNPNIface;
    nsDataHashtable<nsPtrHashKey<void>, PluginIdentifierParent*> mIdentifiers;
    nsNPAPIPlugin* mPlugin;
    ScopedRunnableMethodFactory<PluginModuleParent> mTaskFactory;
    nsString mPluginDumpID;
    nsString mBrowserDumpID;
    nsString mHangID;
#ifdef XP_WIN
    InfallibleTArray<float> mPluginCpuUsageOnHang;
#endif

#ifdef MOZ_X11
    // Dup of plugin's X socket, used to scope its resources to this
    // object instead of the plugin process's lifetime
    ScopedClose mPluginXSocketFdDup;
#endif

    friend class mozilla::dom::CrashReporterParent;

#ifdef MOZ_CRASHREPORTER_INJECTOR
    void InitializeInjector();
    
    void OnCrash(DWORD processID) MOZ_OVERRIDE;

    DWORD mFlashProcess1;
    DWORD mFlashProcess2;
#endif
};

} // namespace plugins
} // namespace mozilla

#endif  // ifndef dom_plugins_PluginModuleParent_h
