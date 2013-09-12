/* -*- Mode: c++; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AndroidBridge_h__
#define AndroidBridge_h__

#include <jni.h>
#include <android/log.h>
#include <cstdlib>
#include <pthread.h>

#include "nsCOMPtr.h"
#include "nsCOMArray.h"
#include "nsIRunnable.h"
#include "nsIObserver.h"
#include "nsThreadUtils.h"

#include "AndroidLayerViewWrapper.h"
#include "AndroidJavaWrappers.h"

#include "nsIMutableArray.h"
#include "nsIMIMEInfo.h"
#include "nsColor.h"
#include "gfxRect.h"

#include "nsIAndroidBridge.h"
#include "nsISmsRequest.h"

#include "mozilla/Likely.h"
#include "mozilla/StaticPtr.h"

// Some debug #defines
// #define DEBUG_ANDROID_EVENTS
// #define DEBUG_ANDROID_WIDGET

class nsWindow;
class nsIDOMMozSmsMessage;

/* See the comment in AndroidBridge about this function before using it */
extern "C" JNIEnv * GetJNIForThread();
extern "C" jclass jsjni_FindClass(const char *className);
extern "C" jmethodID jsjni_GetStaticMethodID(jclass methodClass,
                                       const char *methodName,
                                       const char *signature);
extern "C" bool jsjni_ExceptionCheck();
extern "C" void jsjni_CallStaticVoidMethodA(jclass cls, jmethodID method, jvalue *values);
extern "C" int jsjni_CallStaticIntMethodA(jclass cls, jmethodID method, jvalue *values);

extern bool mozilla_AndroidBridge_SetMainThread(void *);
extern jclass GetGeckoAppShellClass();

namespace base {
class Thread;
} // end namespace base

namespace mozilla {

namespace hal {
class BatteryInformation;
class NetworkInformation;
} // namespace hal

namespace dom {
namespace sms {
struct SmsFilterData;
} // namespace sms
} // namespace dom

namespace layers {
class CompositorParent;
} // namespace layers

// The order and number of the members in this structure must correspond
// to the attrsAppearance array in GeckoAppShell.getSystemColors()
typedef struct AndroidSystemColors {
    nscolor textColorPrimary;
    nscolor textColorPrimaryInverse;
    nscolor textColorSecondary;
    nscolor textColorSecondaryInverse;
    nscolor textColorTertiary;
    nscolor textColorTertiaryInverse;
    nscolor textColorHighlight;
    nscolor colorForeground;
    nscolor colorBackground;
    nscolor panelColorForeground;
    nscolor panelColorBackground;
} AndroidSystemColors;

class nsFilePickerCallback : nsISupports {
public:
    NS_DECL_ISUPPORTS
    virtual void handleResult(nsAString& filePath) = 0;
    nsFilePickerCallback() {}
protected:
    virtual ~nsFilePickerCallback() {}
};

class AndroidBridge
{
public:
    enum {
        NOTIFY_IME_RESETINPUTSTATE = 0,
        NOTIFY_IME_REPLY_EVENT = 1,
        NOTIFY_IME_CANCELCOMPOSITION = 2,
        NOTIFY_IME_FOCUSCHANGE = 3
    };

    enum {
        LAYER_CLIENT_TYPE_NONE = 0,
        LAYER_CLIENT_TYPE_GL = 2            // AndroidGeckoGLLayerClient
    };

    static void ConstructBridge(JNIEnv *jEnv, jclass jGeckoAppShellClass);

    static AndroidBridge *Bridge() {
        return sBridge;
    }

    static JavaVM *GetVM() {
        if (MOZ_LIKELY(sBridge))
            return sBridge->mJavaVM;
        return nullptr;
    }

    static JNIEnv *GetJNIEnv() {
        if (MOZ_LIKELY(sBridge)) {
            if ((void*)pthread_self() != sBridge->mThread) {
                __android_log_print(ANDROID_LOG_INFO, "AndroidBridge",
                                    "###!!!!!!! Something's grabbing the JNIEnv from the wrong thread! (thr %p should be %p)",
                                    (void*)pthread_self(), (void*)sBridge->mThread);
                return nullptr;
            }
            return sBridge->mJNIEnv;

        }
        return nullptr;
    }
    
    static jclass GetGeckoAppShellClass() {
        return sBridge->mGeckoAppShellClass;
    }

    // The bridge needs to be constructed via ConstructBridge first,
    // and then once the Gecko main thread is spun up (Gecko side),
    // SetMainThread should be called which will create the JNIEnv for
    // us to use.  toolkit/xre/nsAndroidStartup.cpp calls
    // SetMainThread.
    bool SetMainThread(void *thr);

    /* These are all implemented in Java */
    static void NotifyIME(int aType, int aState);

    static void NotifyIMEEnabled(int aState, const nsAString& aTypeHint,
                                 const nsAString& aModeHint, const nsAString& aActionHint);

    static void NotifyIMEChange(const PRUnichar *aText, uint32_t aTextLen, int aStart, int aEnd, int aNewEnd);

    nsresult CaptureThumbnail(nsIDOMWindow *window, int32_t bufW, int32_t bufH, int32_t tabId, jobject buffer);
    nsresult GetDisplayPort(bool aPageSizeUpdate, bool aIsBrowserContentDisplayed, int32_t tabId, nsIAndroidViewport* metrics, nsIAndroidDisplayport** displayPort);

    bool ProgressiveUpdateCallback(bool aHasPendingNewThebesContent, const gfx::Rect& aDisplayPort, float aDisplayResolution, bool aDrawingCritical, gfx::Rect& aViewport, float& aScaleX, float& aScaleY);

    void AcknowledgeEventSync();

    void EnableLocation(bool aEnable);
    void EnableLocationHighAccuracy(bool aEnable);

    void EnableSensor(int aSensorType);

    void DisableSensor(int aSensorType);

    void NotifyXreExit();

    void ScheduleRestart();

    void SetLayerClient(JNIEnv* env, jobject jobj);
    AndroidGeckoLayerClient &GetLayerClient() { return *mLayerClient; }

    void SetSurfaceView(jobject jobj);
    AndroidGeckoSurfaceView& SurfaceView() { return mSurfaceView; }

    bool GetHandlersForURL(const char *aURL, 
                             nsIMutableArray* handlersArray = nullptr,
                             nsIHandlerApp **aDefaultApp = nullptr,
                             const nsAString& aAction = EmptyString());

    bool GetHandlersForMimeType(const char *aMimeType,
                                  nsIMutableArray* handlersArray = nullptr,
                                  nsIHandlerApp **aDefaultApp = nullptr,
                                  const nsAString& aAction = EmptyString());

    bool OpenUriExternal(const nsACString& aUriSpec, const nsACString& aMimeType,
                           const nsAString& aPackageName = EmptyString(),
                           const nsAString& aClassName = EmptyString(),
                           const nsAString& aAction = EmptyString(),
                           const nsAString& aTitle = EmptyString());

    void GetMimeTypeFromExtensions(const nsACString& aFileExt, nsCString& aMimeType);
    void GetExtensionFromMimeType(const nsACString& aMimeType, nsACString& aFileExt);

    void MoveTaskToBack();

    bool GetClipboardText(nsAString& aText);

    void SetClipboardText(const nsAString& aText);
    
    void EmptyClipboard();

    bool ClipboardHasText();

    void ShowAlertNotification(const nsAString& aImageUrl,
                               const nsAString& aAlertTitle,
                               const nsAString& aAlertText,
                               const nsAString& aAlertData,
                               nsIObserver *aAlertListener,
                               const nsAString& aAlertName);

    void AlertsProgressListener_OnProgress(const nsAString& aAlertName,
                                           int64_t aProgress,
                                           int64_t aProgressMax,
                                           const nsAString& aAlertText);

    void AlertsProgressListener_OnCancel(const nsAString& aAlertName);

    int GetDPI();

    void ShowFilePickerForExtensions(nsAString& aFilePath, const nsAString& aExtensions);
    void ShowFilePickerForMimeType(nsAString& aFilePath, const nsAString& aMimeType);
    void ShowFilePickerAsync(const nsAString& aMimeType, nsFilePickerCallback* callback);

    void PerformHapticFeedback(bool aIsLongPress);

    void Vibrate(const nsTArray<uint32_t>& aPattern);
    void CancelVibrate();

    void SetFullScreen(bool aFullScreen);

    void ShowInputMethodPicker();

    void NotifyDefaultPrevented(bool aDefaultPrevented);

    void HideProgressDialogOnce();

    bool IsNetworkLinkUp();

    bool IsNetworkLinkKnown();

    void SetSelectedLocale(const nsAString&);

    void GetSystemColors(AndroidSystemColors *aColors);

    void GetIconForExtension(const nsACString& aFileExt, uint32_t aIconSize, uint8_t * const aBuf);

    bool GetShowPasswordSetting();

    /* See GLHelpers.java as to why this is needed */
    void *CallEglCreateWindowSurface(void *dpy, void *config, AndroidGeckoSurfaceView& surfaceView);

    // Switch Java to composite with the Gecko Compositor thread
    void RegisterCompositor(JNIEnv* env = NULL, bool resetting = false);
    EGLSurface ProvideEGLSurface();

    bool GetStaticStringField(const char *classID, const char *field, nsAString &result, JNIEnv* env = nullptr);

    bool GetStaticIntField(const char *className, const char *fieldName, int32_t* aInt, JNIEnv* env = nullptr);

    void SetKeepScreenOn(bool on);

    void ScanMedia(const nsAString& aFile, const nsACString& aMimeType);

    void CreateShortcut(const nsAString& aTitle, const nsAString& aURI, const nsAString& aIconData, const nsAString& aIntent);

    // These next four functions are for native Bitmap access in Android 2.2+
    bool HasNativeBitmapAccess();

    bool ValidateBitmap(jobject bitmap, int width, int height);

    void *LockBitmap(jobject bitmap);

    void UnlockBitmap(jobject bitmap);

    bool UnlockProfile();

    void KillAnyZombies();

    /* Copied from Android's native_window.h in newer (platform 9) NDK */
    enum {
        WINDOW_FORMAT_RGBA_8888          = 1,
        WINDOW_FORMAT_RGBX_8888          = 2,
        WINDOW_FORMAT_RGB_565            = 4
    };

    bool HasNativeWindowAccess();

    void *AcquireNativeWindow(JNIEnv* aEnv, jobject aSurface);
    void ReleaseNativeWindow(void *window);

    void *AcquireNativeWindowFromSurfaceTexture(JNIEnv* aEnv, jobject aSurface);
    void ReleaseNativeWindowForSurfaceTexture(void *window);

    bool SetNativeWindowFormat(void *window, int width, int height, int format);

    bool LockWindow(void *window, unsigned char **bits, int *width, int *height, int *format, int *stride);
    bool UnlockWindow(void *window);
    
    void HandleGeckoMessage(const nsAString& message, nsAString &aRet);

    void CheckURIVisited(const nsAString& uri);
    void MarkURIVisited(const nsAString& uri);

    bool InitCamera(const nsCString& contentType, uint32_t camera, uint32_t *width, uint32_t *height, uint32_t *fps);

    void CloseCamera();

    void EnableBatteryNotifications();
    void DisableBatteryNotifications();
    void GetCurrentBatteryInformation(hal::BatteryInformation* aBatteryInfo);

    uint16_t GetNumberOfMessagesForText(const nsAString& aText);
    void SendMessage(const nsAString& aNumber, const nsAString& aText, nsISmsRequest* aRequest);
    void GetMessage(int32_t aMessageId, nsISmsRequest* aRequest);
    void DeleteMessage(int32_t aMessageId, nsISmsRequest* aRequest);
    void CreateMessageList(const dom::sms::SmsFilterData& aFilter, bool aReverse, nsISmsRequest* aRequest);
    void GetNextMessageInList(int32_t aListId, nsISmsRequest* aRequest);
    void ClearMessageList(int32_t aListId);
    already_AddRefed<nsISmsRequest> DequeueSmsRequest(int32_t aRequestId);

    bool IsTablet();

    void GetCurrentNetworkInformation(hal::NetworkInformation* aNetworkInfo);
    void EnableNetworkNotifications();
    void DisableNetworkNotifications();

    void SetFirstPaintViewport(const nsIntPoint& aOffset, float aZoom, const nsIntRect& aPageRect, const gfx::Rect& aCssPageRect);
    void SetPageRect(const gfx::Rect& aCssPageRect);
    void SyncViewportInfo(const nsIntRect& aDisplayPort, float aDisplayResolution, bool aLayersUpdated,
                          nsIntPoint& aScrollOffset, float& aScaleX, float& aScaleY);

    void AddPluginView(jobject view, const gfxRect& rect, bool isFullScreen);
    void RemovePluginView(jobject view, bool isFullScreen);

    // These methods don't use a ScreenOrientation because it's an
    // enum and that would require including the header which requires
    // include IPC headers which requires including basictypes.h which
    // requires a lot of changes...
    uint32_t GetScreenOrientation();
    void EnableScreenOrientationNotifications();
    void DisableScreenOrientationNotifications();
    void LockScreenOrientation(uint32_t aOrientation);
    void UnlockScreenOrientation();

    bool PumpMessageLoop();

    void NotifyWakeLockChanged(const nsAString& topic, const nsAString& state);

    int GetAPIVersion() { return mAPIVersion; }
    bool IsHoneycomb() { return mAPIVersion >= 11 && mAPIVersion <= 13; }

    void ScheduleComposite();
    void RegisterSurfaceTextureFrameListener(jobject surfaceTexture, int id);
    void UnregisterSurfaceTextureFrameListener(jobject surfaceTexture);

    void GetGfxInfoData(nsACString& aRet);
    nsresult GetProxyForURI(const nsACString & aSpec,
                            const nsACString & aScheme,
                            const nsACString & aHost,
                            const int32_t      aPort,
                            nsACString & aResult);
protected:
    static AndroidBridge *sBridge;
    static StaticAutoPtr<nsTArray<nsCOMPtr<nsISmsRequest> > > sSmsRequests;

    // the global JavaVM
    JavaVM *mJavaVM;

    // the JNIEnv for the main thread
    JNIEnv *mJNIEnv;
    void *mThread;

    // the GeckoSurfaceView
    AndroidGeckoSurfaceView mSurfaceView;

    AndroidGeckoLayerClient *mLayerClient;

    // the GeckoAppShell java class
    jclass mGeckoAppShellClass;

    AndroidBridge();
    ~AndroidBridge();

    bool Init(JNIEnv *jEnv, jclass jGeckoApp);

    bool mOpenedGraphicsLibraries;
    void OpenGraphicsLibraries();
    void* GetNativeSurface(JNIEnv* env, jobject surface);

    bool mHasNativeBitmapAccess;
    bool mHasNativeWindowAccess;
    bool mHasNativeWindowFallback;

    int mAPIVersion;

    int32_t QueueSmsRequest(nsISmsRequest* aRequest);

    // other things
    jmethodID jNotifyIME;
    jmethodID jNotifyIMEEnabled;
    jmethodID jNotifyIMEChange;
    jmethodID jAcknowledgeEventSync;
    jmethodID jEnableLocation;
    jmethodID jEnableLocationHighAccuracy;
    jmethodID jEnableSensor;
    jmethodID jDisableSensor;
    jmethodID jNotifyAppShellReady;
    jmethodID jNotifyXreExit;
    jmethodID jScheduleRestart;
    jmethodID jGetOutstandingDrawEvents;
    jmethodID jGetHandlersForMimeType;
    jmethodID jGetHandlersForURL;
    jmethodID jOpenUriExternal;
    jmethodID jGetMimeTypeFromExtensions;
    jmethodID jGetExtensionFromMimeType;
    jmethodID jMoveTaskToBack;
    jmethodID jGetClipboardText;
    jmethodID jSetClipboardText;
    jmethodID jShowAlertNotification;
    jmethodID jShowFilePickerForExtensions;
    jmethodID jShowFilePickerForMimeType;
    jmethodID jShowFilePickerAsync;
    jmethodID jUnlockProfile;
    jmethodID jKillAnyZombies;
    jmethodID jAlertsProgressListener_OnProgress;
    jmethodID jAlertsProgressListener_OnCancel;
    jmethodID jGetDpi;
    jmethodID jSetFullScreen;
    jmethodID jShowInputMethodPicker;
    jmethodID jNotifyDefaultPrevented;
    jmethodID jHideProgressDialog;
    jmethodID jPerformHapticFeedback;
    jmethodID jVibrate1;
    jmethodID jVibrateA;
    jmethodID jCancelVibrate;
    jmethodID jSetKeepScreenOn;
    jmethodID jIsNetworkLinkUp;
    jmethodID jIsNetworkLinkKnown;
    jmethodID jSetSelectedLocale;
    jmethodID jScanMedia;
    jmethodID jGetSystemColors;
    jmethodID jGetIconForExtension;
    jmethodID jCreateShortcut;
    jmethodID jGetShowPasswordSetting;
    jmethodID jPostToJavaThread;
    jmethodID jInitCamera;
    jmethodID jCloseCamera;
    jmethodID jIsTablet;
    jmethodID jEnableBatteryNotifications;
    jmethodID jDisableBatteryNotifications;
    jmethodID jGetCurrentBatteryInformation;
    jmethodID jHandleGeckoMessage;
    jmethodID jCheckUriVisited;
    jmethodID jMarkUriVisited;
    jmethodID jAddPluginView;
    jmethodID jRemovePluginView;
    jmethodID jCreateSurface;
    jmethodID jShowSurface;
    jmethodID jHideSurface;
    jmethodID jDestroySurface;
    jmethodID jGetProxyForURI;

    jmethodID jNumberOfMessages;
    jmethodID jSendMessage;
    jmethodID jGetMessage;
    jmethodID jDeleteMessage;
    jmethodID jCreateMessageList;
    jmethodID jGetNextMessageinList;
    jmethodID jClearMessageList;

    jmethodID jGetCurrentNetworkInformation;
    jmethodID jEnableNetworkNotifications;
    jmethodID jDisableNetworkNotifications;

    jmethodID jGetScreenOrientation;
    jmethodID jEnableScreenOrientationNotifications;
    jmethodID jDisableScreenOrientationNotifications;
    jmethodID jLockScreenOrientation;
    jmethodID jUnlockScreenOrientation;
    jmethodID jPumpMessageLoop;
    jmethodID jNotifyWakeLockChanged;
    jmethodID jRegisterSurfaceTextureFrameListener;
    jmethodID jUnregisterSurfaceTextureFrameListener;

    jclass jThumbnailHelperClass;
    jmethodID jNotifyThumbnail;

    // for GfxInfo (gfx feature detection and blacklisting)
    jmethodID jGetGfxInfoData;

    // For native surface stuff
    jclass jSurfaceClass;
    jfieldID jSurfacePointerField;

    // stuff we need for CallEglCreateWindowSurface
    jclass jEGLSurfaceImplClass;
    jclass jEGLContextImplClass;
    jclass jEGLConfigImplClass;
    jclass jEGLDisplayImplClass;
    jclass jEGLContextClass;
    jclass jEGL10Class;

    jclass jLayerView;
    jmethodID jRegisterCompositorMethod;

    // some convinient types to have around
    jclass jStringClass;

    // calls we've dlopened from libjnigraphics.so
    int (* AndroidBitmap_getInfo)(JNIEnv *env, jobject bitmap, void *info);
    int (* AndroidBitmap_lockPixels)(JNIEnv *env, jobject bitmap, void **buffer);
    int (* AndroidBitmap_unlockPixels)(JNIEnv *env, jobject bitmap);

    void* (*ANativeWindow_fromSurface)(JNIEnv *env, jobject surface);
    void* (*ANativeWindow_fromSurfaceTexture)(JNIEnv *env, jobject surfaceTexture);
    void (*ANativeWindow_release)(void *window);
    int (*ANativeWindow_setBuffersGeometry)(void *window, int width, int height, int format);

    int (* ANativeWindow_lock)(void *window, void *outBuffer, void *inOutDirtyBounds);
    int (* ANativeWindow_unlockAndPost)(void *window);

    int (* Surface_lock)(void* surface, void* surfaceInfo, void* region, bool block);
    int (* Surface_unlockAndPost)(void* surface);
    void (* Region_constructor)(void* region);
    void (* Region_set)(void* region, void* rect);
};

class AutoJObject {
public:
    AutoJObject(JNIEnv* aJNIEnv = NULL) : mObject(NULL)
    {
        mJNIEnv = aJNIEnv ? aJNIEnv : AndroidBridge::GetJNIEnv();
    }

    AutoJObject(JNIEnv* aJNIEnv, jobject aObject)
    {
        mJNIEnv = aJNIEnv ? aJNIEnv : AndroidBridge::GetJNIEnv();
        mObject = aObject;
    }

    ~AutoJObject() {
        if (mObject)
            mJNIEnv->DeleteLocalRef(mObject);
    }

    jobject operator=(jobject aObject)
    {
        if (mObject) {
            mJNIEnv->DeleteLocalRef(mObject);
        }
        return mObject = aObject;
    }

    operator jobject() {
        return mObject;
    }
private:
    JNIEnv* mJNIEnv;
    jobject mObject;
};

class AutoLocalJNIFrame {
public:
    AutoLocalJNIFrame(int nEntries = 128)
        : mEntries(nEntries), mHasFrameBeenPushed(false)
    {
        mJNIEnv = AndroidBridge::GetJNIEnv();
        Push();
    }

    AutoLocalJNIFrame(JNIEnv* aJNIEnv, int nEntries = 128)
        : mEntries(nEntries), mHasFrameBeenPushed(false)
    {
        mJNIEnv = aJNIEnv ? aJNIEnv : AndroidBridge::GetJNIEnv();

        Push();
    }

    // Note! Calling Purge makes all previous local refs created in
    // the AutoLocalJNIFrame's scope INVALID; be sure that you locked down
    // any local refs that you need to keep around in global refs!
    void Purge() {
        if (mJNIEnv) {
            if (mHasFrameBeenPushed)
                mJNIEnv->PopLocalFrame(NULL);
            Push();
        }
    }

    JNIEnv* GetEnv() {
        return mJNIEnv;
    }

    bool CheckForException() {
        if (mJNIEnv->ExceptionCheck()) {
            mJNIEnv->ExceptionDescribe();
            mJNIEnv->ExceptionClear();
            return true;
        }

        return false;
    }

    ~AutoLocalJNIFrame() {
        if (!mJNIEnv)
            return;

        CheckForException();

        if (mHasFrameBeenPushed)
            mJNIEnv->PopLocalFrame(NULL);
    }

private:
    void Push() {
        if (!mJNIEnv)
            return;

        // Make sure there is enough space to store a local ref to the
        // exception.  I am not completely sure this is needed, but does
        // not hurt.
        jint ret = mJNIEnv->PushLocalFrame(mEntries + 1);
        NS_ABORT_IF_FALSE(ret == 0, "Failed to push local JNI frame");
        if (ret < 0)
            CheckForException();
        else
            mHasFrameBeenPushed = true;
    }

    int mEntries;
    JNIEnv* mJNIEnv;
    bool mHasFrameBeenPushed;
};

}

#define NS_ANDROIDBRIDGE_CID \
{ 0x0FE2321D, 0xEBD9, 0x467D, \
    { 0xA7, 0x43, 0x03, 0xA6, 0x8D, 0x40, 0x59, 0x9E } }

class nsAndroidBridge : public nsIAndroidBridge
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIANDROIDBRIDGE

  nsAndroidBridge();

private:
  ~nsAndroidBridge();

protected:
};


#endif /* AndroidBridge_h__ */
