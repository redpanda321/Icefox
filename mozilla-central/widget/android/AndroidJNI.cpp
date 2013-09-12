/* -*- Mode: c++; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Hal.h"
#include "nsIFile.h"
#include "nsString.h"

#include "AndroidBridge.h"
#include "AndroidGraphicBuffer.h"

#include <jni.h>
#include <pthread.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

#include "nsAppShell.h"
#include "nsWindow.h"
#include <android/log.h>
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "nsINetworkLinkService.h"

#ifdef MOZ_CRASHREPORTER
#include "nsICrashReporter.h"
#include "nsExceptionHandler.h"
#endif

#include "mozilla/unused.h"

#include "mozilla/dom/sms/SmsMessage.h"
#include "mozilla/dom/sms/Constants.h"
#include "mozilla/dom/sms/Types.h"
#include "mozilla/dom/sms/PSms.h"
#include "mozilla/dom/sms/SmsParent.h"
#include "nsISmsDatabaseService.h"
#include "nsPluginInstanceOwner.h"
#include "nsSurfaceTexture.h"

using namespace mozilla;
using namespace mozilla::dom::sms;

/* Forward declare all the JNI methods as extern "C" */

extern "C" {
/*
 * Incoming JNI methods
 */

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_nativeInit(JNIEnv *jenv, jclass jc)
{
    AndroidBridge::ConstructBridge(jenv, jc);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifyGeckoOfEvent(JNIEnv *jenv, jclass jc, jobject event)
{
    // poke the appshell
    if (nsAppShell::gAppShell)
        nsAppShell::gAppShell->PostEvent(new AndroidGeckoEvent(jenv, event));
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_processNextNativeEvent(JNIEnv *jenv, jclass)
{
    // poke the appshell
    if (nsAppShell::gAppShell)
        nsAppShell::gAppShell->ProcessNextNativeEvent(false);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_setSurfaceView(JNIEnv *jenv, jclass, jobject obj)
{
    AndroidBridge::Bridge()->SetSurfaceView(jenv->NewGlobalRef(obj));
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_setLayerClient(JNIEnv *jenv, jclass, jobject obj)
{
    AndroidBridge::Bridge()->SetLayerClient(jenv, obj);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_onLowMemory(JNIEnv *jenv, jclass jc)
{
    if (nsAppShell::gAppShell) {
        nsAppShell::gAppShell->NotifyObservers(nullptr,
                                               "memory-pressure",
                                               NS_LITERAL_STRING("low-memory").get());
    }
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_onResume(JNIEnv *jenv, jclass jc)
{
    if (nsAppShell::gAppShell)
        nsAppShell::gAppShell->OnResume();
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_callObserver(JNIEnv *jenv, jclass, jstring jObserverKey, jstring jTopic, jstring jData)
{
    if (!nsAppShell::gAppShell)
        return;

    nsJNIString sObserverKey(jObserverKey, jenv);
    nsJNIString sTopic(jTopic, jenv);
    nsJNIString sData(jData, jenv);

    nsAppShell::gAppShell->CallObserver(sObserverKey, sTopic, sData);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_removeObserver(JNIEnv *jenv, jclass, jstring jObserverKey)
{
    if (!nsAppShell::gAppShell)
        return;

    const jchar *observerKey = jenv->GetStringChars(jObserverKey, NULL);
    nsString sObserverKey(observerKey);
    sObserverKey.SetLength(jenv->GetStringLength(jObserverKey));
    jenv->ReleaseStringChars(jObserverKey, observerKey);

    nsAppShell::gAppShell->RemoveObserver(sObserverKey);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_onChangeNetworkLinkStatus(JNIEnv *jenv, jclass, jstring jStatus)
{
    if (!nsAppShell::gAppShell)
        return;

    nsJNIString sStatus(jStatus, jenv);

    nsAppShell::gAppShell->NotifyObservers(nullptr,
                                           NS_NETWORK_LINK_TOPIC,
                                           sStatus.get());
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_reportJavaCrash(JNIEnv *jenv, jclass, jstring jStackTrace)
{
#ifdef MOZ_CRASHREPORTER
    const nsJNIString stackTrace16(jStackTrace, jenv);
    const NS_ConvertUTF16toUTF8 stackTrace8(stackTrace16);
    CrashReporter::AnnotateCrashReport(NS_LITERAL_CSTRING("JavaStackTrace"), stackTrace8);
#endif // MOZ_CRASHREPORTER

    abort();
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifyBatteryChange(JNIEnv* jenv, jclass,
                                                         jdouble aLevel,
                                                         jboolean aCharging,
                                                         jdouble aRemainingTime)
{
    class NotifyBatteryChangeRunnable : public nsRunnable {
    public:
      NotifyBatteryChangeRunnable(double aLevel, bool aCharging, double aRemainingTime)
        : mLevel(aLevel)
        , mCharging(aCharging)
        , mRemainingTime(aRemainingTime)
      {}

      NS_IMETHODIMP Run() {
        hal::NotifyBatteryChange(hal::BatteryInformation(mLevel, mCharging, mRemainingTime));
        return NS_OK;
      }

    private:
      double mLevel;
      bool   mCharging;
      double mRemainingTime;
    };

    nsCOMPtr<nsIRunnable> runnable = new NotifyBatteryChangeRunnable(aLevel, aCharging, aRemainingTime);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifySmsReceived(JNIEnv* jenv, jclass,
                                                       jstring aSender,
                                                       jstring aBody,
                                                       jint aMessageClass,
                                                       jlong aTimestamp)
{
    class NotifySmsReceivedRunnable : public nsRunnable {
    public:
      NotifySmsReceivedRunnable(const SmsMessageData& aMessageData)
        : mMessageData(aMessageData)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
        if (!obs) {
          return NS_OK;
        }

        nsCOMPtr<nsIDOMMozSmsMessage> message = new SmsMessage(mMessageData);
        obs->NotifyObservers(message, kSmsReceivedObserverTopic, nullptr);
        return NS_OK;
      }

    private:
      SmsMessageData mMessageData;
    };

    SmsMessageData message(0, eDeliveryState_Received, eDeliveryStatus_Success,
                           nsJNIString(aSender, jenv), EmptyString(),
                           nsJNIString(aBody, jenv),
                           static_cast<MessageClass>(aMessageClass),
                           aTimestamp, false);

    nsCOMPtr<nsIRunnable> runnable = new NotifySmsReceivedRunnable(message);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifySmsSent(JNIEnv* jenv, jclass,
                                                   jint aId,
                                                   jstring aReceiver,
                                                   jstring aBody,
                                                   jlong aTimestamp,
                                                   jint aRequestId)
{
    class NotifySmsSentRunnable : public nsRunnable {
    public:
      NotifySmsSentRunnable(const SmsMessageData& aMessageData,
                            int32_t aRequestId)
        : mMessageData(aMessageData)
        , mRequestId(aRequestId)
      {}

      NS_IMETHODIMP Run() {
        /*
         * First, we are going to notify all SmsManager that a message has
         * been sent. Then, we will notify the SmsRequest object about it.
         */
        nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
        if (!obs) {
          return NS_OK;
        }

        nsCOMPtr<nsIDOMMozSmsMessage> message = new SmsMessage(mMessageData);
        obs->NotifyObservers(message, kSmsSentObserverTopic, nullptr);

        nsCOMPtr<nsISmsRequest> request =
          AndroidBridge::Bridge()->DequeueSmsRequest(mRequestId);
        NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

        request->NotifyMessageSent(message);
        return NS_OK;
      }

    private:
      SmsMessageData mMessageData;
      int32_t        mRequestId;
    };

    // TODO Need to add the message `messageClass` parameter value. Bug 804476
    SmsMessageData message(aId, eDeliveryState_Sent, eDeliveryStatus_Pending,
                           EmptyString(), nsJNIString(aReceiver, jenv),
                           nsJNIString(aBody, jenv), eMessageClass_Normal,
                           aTimestamp, true);

    nsCOMPtr<nsIRunnable> runnable = new NotifySmsSentRunnable(message, aRequestId);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifySmsDelivery(JNIEnv* jenv, jclass,
                                                       jint aId,
                                                       jint aDeliveryStatus,
                                                       jstring aReceiver,
                                                       jstring aBody,
                                                       jlong aTimestamp)
{
    class NotifySmsDeliveredRunnable : public nsRunnable {
    public:
      NotifySmsDeliveredRunnable(const SmsMessageData& aMessageData)
        : mMessageData(aMessageData)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
        if (!obs) {
          return NS_OK;
        }

        nsCOMPtr<nsIDOMMozSmsMessage> message = new SmsMessage(mMessageData);
        const char* topic = (mMessageData.deliveryStatus() == eDeliveryStatus_Success)
                            ? kSmsDeliverySuccessObserverTopic
                            : kSmsDeliveryErrorObserverTopic;
        obs->NotifyObservers(message, topic, nullptr);

        return NS_OK;
      }

    private:
      SmsMessageData mMessageData;
    };

    // TODO Need to add the message `messageClass` parameter value. Bug 804476
    SmsMessageData message(aId, eDeliveryState_Sent,
                           static_cast<DeliveryStatus>(aDeliveryStatus),
                           EmptyString(), nsJNIString(aReceiver, jenv),
                           nsJNIString(aBody, jenv), eMessageClass_Normal,
                           aTimestamp, true);

    nsCOMPtr<nsIRunnable> runnable = new NotifySmsDeliveredRunnable(message);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifySmsSendFailed(JNIEnv* jenv, jclass,
                                                         jint aError,
                                                         jint aRequestId)
{
    class NotifySmsSendFailedRunnable : public nsRunnable {
    public:
      NotifySmsSendFailedRunnable(int32_t aError,
                                  int32_t aRequestId)
        : mError(aError)
        , mRequestId(aRequestId)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsISmsRequest> request =
          AndroidBridge::Bridge()->DequeueSmsRequest(mRequestId);
        NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

        request->NotifySendMessageFailed(mError);
        return NS_OK;
      }

    private:
      int32_t  mError;
      int32_t  mRequestId;
    };


    nsCOMPtr<nsIRunnable> runnable =
      new NotifySmsSendFailedRunnable(aError, aRequestId);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifyGetSms(JNIEnv* jenv, jclass,
                                                  jint aId,
                                                  jint aDeliveryStatus,
                                                  jstring aReceiver,
                                                  jstring aSender,
                                                  jstring aBody,
                                                  jlong aTimestamp,
                                                  jint aRequestId)
{
    class NotifyGetSmsRunnable : public nsRunnable {
    public:
      NotifyGetSmsRunnable(const SmsMessageData& aMessageData,
                           int32_t aRequestId)
        : mMessageData(aMessageData)
        , mRequestId(aRequestId)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsISmsRequest> request =
          AndroidBridge::Bridge()->DequeueSmsRequest(mRequestId);
        NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

        nsCOMPtr<nsIDOMMozSmsMessage> message = new SmsMessage(mMessageData);
        request->NotifyMessageGot(message);
        return NS_OK;
      }

    private:
      SmsMessageData mMessageData;
      int32_t        mRequestId;
    };

    nsJNIString receiver = nsJNIString(aReceiver, jenv);
    DeliveryState state = receiver.IsEmpty() ? eDeliveryState_Received
                                             : eDeliveryState_Sent;

    // TODO Need to add the message `read` parameter value. Bug 748391
    // TODO Need to add the message `messageClass` parameter value. Bug 804476
    SmsMessageData message(aId, state,
                           static_cast<DeliveryStatus>(aDeliveryStatus),
                           nsJNIString(aSender, jenv), receiver,
                           nsJNIString(aBody, jenv), eMessageClass_Normal,
                           aTimestamp, true);

    nsCOMPtr<nsIRunnable> runnable = new NotifyGetSmsRunnable(message, aRequestId);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifyGetSmsFailed(JNIEnv* jenv, jclass,
                                                        jint aError,
                                                        jint aRequestId)
{
    class NotifyGetSmsFailedRunnable : public nsRunnable {
    public:
      NotifyGetSmsFailedRunnable(int32_t aError,
                                 int32_t aRequestId)
        : mError(aError)
        , mRequestId(aRequestId)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsISmsRequest> request =
          AndroidBridge::Bridge()->DequeueSmsRequest(mRequestId);
        NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

        request->NotifyGetMessageFailed(mError);
        return NS_OK;
      }

    private:
      int32_t  mError;
      int32_t  mRequestId;
    };


    nsCOMPtr<nsIRunnable> runnable =
      new NotifyGetSmsFailedRunnable(aError, aRequestId);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifySmsDeleted(JNIEnv* jenv, jclass,
                                                      jboolean aDeleted,
                                                      jint aRequestId)
{
    class NotifySmsDeletedRunnable : public nsRunnable {
    public:
      NotifySmsDeletedRunnable(bool aDeleted, int32_t aRequestId)
        : mDeleted(aDeleted)
        , mRequestId(aRequestId)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsISmsRequest> request =
          AndroidBridge::Bridge()->DequeueSmsRequest(mRequestId);
        NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

        request->NotifyMessageDeleted(mDeleted);
        return NS_OK;
      }

    private:
      bool      mDeleted;
      int32_t   mRequestId;
    };


    nsCOMPtr<nsIRunnable> runnable =
      new NotifySmsDeletedRunnable(aDeleted, aRequestId);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifySmsDeleteFailed(JNIEnv* jenv, jclass,
                                                           jint aError,
                                                           jint aRequestId)
{
    class NotifySmsDeleteFailedRunnable : public nsRunnable {
    public:
      NotifySmsDeleteFailedRunnable(int32_t aError,
                                    int32_t aRequestId)
        : mError(aError)
        , mRequestId(aRequestId)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsISmsRequest> request =
          AndroidBridge::Bridge()->DequeueSmsRequest(mRequestId);
        NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

        request->NotifyDeleteMessageFailed(mError);
        return NS_OK;
      }

    private:
      int32_t  mError;
      int32_t  mRequestId;
    };


    nsCOMPtr<nsIRunnable> runnable =
      new NotifySmsDeleteFailedRunnable(aError, aRequestId);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifyNoMessageInList(JNIEnv* jenv, jclass,
                                                           jint aRequestId)
{
    class NotifyNoMessageInListRunnable : public nsRunnable {
    public:
      NotifyNoMessageInListRunnable(int32_t aRequestId)
        : mRequestId(aRequestId)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsISmsRequest> request =
          AndroidBridge::Bridge()->DequeueSmsRequest(mRequestId);
        NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

        request->NotifyNoMessageInList();
        return NS_OK;
      }

    private:
      int32_t               mRequestId;
    };


    nsCOMPtr<nsIRunnable> runnable =
      new NotifyNoMessageInListRunnable(aRequestId);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifyListCreated(JNIEnv* jenv, jclass,
                                                       jint aListId,
                                                       jint aMessageId,
                                                       jint aDeliveryStatus,
                                                       jstring aReceiver,
                                                       jstring aSender,
                                                       jstring aBody,
                                                       jlong aTimestamp,
                                                       jint aRequestId)
{
    class NotifyCreateMessageListRunnable : public nsRunnable {
    public:
      NotifyCreateMessageListRunnable(int32_t aListId,
                                      const SmsMessageData& aMessageData,
                                      int32_t aRequestId)
        : mListId(aListId)
        , mMessageData(aMessageData)
        , mRequestId(aRequestId)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsISmsRequest> request =
          AndroidBridge::Bridge()->DequeueSmsRequest(mRequestId);
        NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

        nsCOMPtr<nsIDOMMozSmsMessage> message = new SmsMessage(mMessageData);
        request->NotifyMessageListCreated(mListId, message);
        return NS_OK;
      }

    private:
      int32_t        mListId;
      SmsMessageData mMessageData;
      int32_t        mRequestId;
    };


    nsJNIString receiver = nsJNIString(aReceiver, jenv);
    DeliveryState state = receiver.IsEmpty() ? eDeliveryState_Received
                                             : eDeliveryState_Sent;

    // TODO Need to add the message `read` parameter value. Bug 748391
    // TODO Need to add the message `messageClass` parameter value. Bug 804476
    SmsMessageData message(aMessageId, state,
                           static_cast<DeliveryStatus>(aDeliveryStatus),
                           nsJNIString(aSender, jenv), receiver,
                           nsJNIString(aBody, jenv), eMessageClass_Normal,
                           aTimestamp, true);

    nsCOMPtr<nsIRunnable> runnable =
      new NotifyCreateMessageListRunnable(aListId, message, aRequestId);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifyGotNextMessage(JNIEnv* jenv, jclass,
                                                          jint aMessageId,
                                                          jint aDeliveryStatus,
                                                          jstring aReceiver,
                                                          jstring aSender,
                                                          jstring aBody,
                                                          jlong aTimestamp,
                                                          jint aRequestId)
{
    class NotifyGotNextMessageRunnable : public nsRunnable {
    public:
      NotifyGotNextMessageRunnable(const SmsMessageData& aMessageData,
                                   int32_t aRequestId)
        : mMessageData(aMessageData)
        , mRequestId(aRequestId)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsISmsRequest> request =
          AndroidBridge::Bridge()->DequeueSmsRequest(mRequestId);
        NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

        nsCOMPtr<nsIDOMMozSmsMessage> message = new SmsMessage(mMessageData);
        request->NotifyNextMessageInListGot(message);
        return NS_OK;
      }

    private:
      SmsMessageData mMessageData;
      int32_t        mRequestId;
    };


    nsJNIString receiver = nsJNIString(aReceiver, jenv);
    DeliveryState state = receiver.IsEmpty() ? eDeliveryState_Received
                                             : eDeliveryState_Sent;

    // TODO Need to add the message `read` parameter value. Bug 748391
    // TODO Need to add the message `messageClass` parameter value. Bug 804476
    SmsMessageData message(aMessageId, state,
                           static_cast<DeliveryStatus>(aDeliveryStatus),
                           nsJNIString(aSender, jenv), receiver,
                           nsJNIString(aBody, jenv), eMessageClass_Normal,
                           aTimestamp, true);

    nsCOMPtr<nsIRunnable> runnable =
      new NotifyGotNextMessageRunnable(message, aRequestId);
    NS_DispatchToMainThread(runnable);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifyReadingMessageListFailed(JNIEnv* jenv, jclass,
                                                                    jint aError,
                                                                    jint aRequestId)
{
    class NotifyReadListFailedRunnable : public nsRunnable {
    public:
      NotifyReadListFailedRunnable(int32_t aError,
                                   int32_t aRequestId)
        : mError(aError)
        , mRequestId(aRequestId)
      {}

      NS_IMETHODIMP Run() {
        nsCOMPtr<nsISmsRequest> request =
          AndroidBridge::Bridge()->DequeueSmsRequest(mRequestId);
        NS_ENSURE_TRUE(request, NS_ERROR_FAILURE);

        request->NotifyReadMessageListFailed(mError);
        return NS_OK;
      }

    private:
      int32_t  mError;
      int32_t  mRequestId;
    };


    nsCOMPtr<nsIRunnable> runnable =
      new NotifyReadListFailedRunnable(aError, aRequestId);
    NS_DispatchToMainThread(runnable);
}

#ifdef MOZ_ANDROID_OMTC

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_scheduleComposite(JNIEnv*, jclass)
{
    nsWindow::ScheduleComposite();
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_schedulePauseComposition(JNIEnv*, jclass)
{
    nsWindow::SchedulePauseComposition();
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_scheduleResumeComposition(JNIEnv*, jclass, jint width, jint height)
{
    nsWindow::ScheduleResumeComposition(width, height);
}

NS_EXPORT float JNICALL
Java_org_mozilla_gecko_GeckoAppShell_computeRenderIntegrity(JNIEnv*, jclass)
{
    return nsWindow::ComputeRenderIntegrity();
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_notifyFilePickerResult(JNIEnv* jenv, jclass, jstring filePath, jlong callback)
{
    class NotifyFilePickerResultRunnable : public nsRunnable {
    public:
        NotifyFilePickerResultRunnable(nsString& fileDir, long callback) : 
            mFileDir(fileDir), mCallback(callback) {}

        NS_IMETHODIMP Run() {
            nsFilePickerCallback* handler = (nsFilePickerCallback*)mCallback;
            handler->handleResult(mFileDir);
            handler->Release();
            return NS_OK;
        }
    private:
        nsString mFileDir;
        long mCallback;
    };
    nsString path = nsJNIString(filePath, jenv);
    
    nsCOMPtr<nsIRunnable> runnable =
        new NotifyFilePickerResultRunnable(path, (long)callback);
    NS_DispatchToMainThread(runnable);
}

static int
NextPowerOfTwo(int value) {
    // code taken from http://acius2.blogspot.com/2007/11/calculating-next-power-of-2.html
    if (0 == value--) {
        return 1;
    }
    value = (value >> 1) | value;
    value = (value >> 2) | value;
    value = (value >> 4) | value;
    value = (value >> 8) | value;
    value = (value >> 16) | value;
    return value + 1;
}

#define MAX_LOCK_ATTEMPTS 10

static bool LockWindowWithRetry(void* window, unsigned char** bits, int* width, int* height, int* format, int* stride)
{
  int count = 0;

  while (count < MAX_LOCK_ATTEMPTS) {
      if (AndroidBridge::Bridge()->LockWindow(window, bits, width, height, format, stride))
        return true;

      count++;
      usleep(500);
  }

  return false;
}

NS_EXPORT jobject JNICALL
Java_org_mozilla_gecko_GeckoAppShell_getSurfaceBits(JNIEnv* jenv, jclass, jobject surface)
{
    static jclass jSurfaceBitsClass = nullptr;
    static jmethodID jSurfaceBitsCtor = 0;
    static jfieldID jSurfaceBitsWidth, jSurfaceBitsHeight, jSurfaceBitsFormat, jSurfaceBitsBuffer;

    jobject surfaceBits = nullptr;
    unsigned char* bitsCopy = nullptr;
    int dstWidth, dstHeight, dstSize;

    void* window = AndroidBridge::Bridge()->AcquireNativeWindow(jenv, surface);
    if (!window)
        return nullptr;

    unsigned char* bits;
    int srcWidth, srcHeight, format, srcStride;

    // So we lock/unlock once here in order to get whatever is currently the front buffer. It sucks.
    if (!LockWindowWithRetry(window, &bits, &srcWidth, &srcHeight, &format, &srcStride))
        return nullptr;

    AndroidBridge::Bridge()->UnlockWindow(window);

    // This is lock will result in the front buffer, since the last unlock rotated it to the back. Probably.
    if (!LockWindowWithRetry(window, &bits, &srcWidth, &srcHeight, &format, &srcStride))
        return nullptr;

    // These are from android.graphics.PixelFormat
    int bpp;
    switch (format) {
    case 1: // RGBA_8888
        bpp = 4;
        break;
    case 4: // RGB_565
        bpp = 2;
        break;
    default:
        goto cleanup;
    }

    dstWidth = NextPowerOfTwo(srcWidth);
    dstHeight = NextPowerOfTwo(srcHeight);
    dstSize = dstWidth * dstHeight * bpp;

    bitsCopy = (unsigned char*)malloc(dstSize);
    bzero(bitsCopy, dstSize);
    for (int i = 0; i < srcHeight; i++) {
        memcpy(bitsCopy + ((dstHeight - i - 1) * dstWidth * bpp), bits + (i * srcStride * bpp), srcStride * bpp);
    }
    
    if (!jSurfaceBitsClass) {
        jSurfaceBitsClass = (jclass)jenv->NewGlobalRef(jenv->FindClass("org/mozilla/gecko/SurfaceBits"));
        jSurfaceBitsCtor = jenv->GetMethodID(jSurfaceBitsClass, "<init>", "()V");

        jSurfaceBitsWidth = jenv->GetFieldID(jSurfaceBitsClass, "width", "I");
        jSurfaceBitsHeight = jenv->GetFieldID(jSurfaceBitsClass, "height", "I");
        jSurfaceBitsFormat = jenv->GetFieldID(jSurfaceBitsClass, "format", "I");
        jSurfaceBitsBuffer = jenv->GetFieldID(jSurfaceBitsClass, "buffer", "Ljava/nio/ByteBuffer;");
    }

    surfaceBits = jenv->NewObject(jSurfaceBitsClass, jSurfaceBitsCtor);
    jenv->SetIntField(surfaceBits, jSurfaceBitsWidth, dstWidth);
    jenv->SetIntField(surfaceBits, jSurfaceBitsHeight, dstHeight);
    jenv->SetIntField(surfaceBits, jSurfaceBitsFormat, format);
    jenv->SetObjectField(surfaceBits, jSurfaceBitsBuffer, jenv->NewDirectByteBuffer(bitsCopy, dstSize));

cleanup:
    AndroidBridge::Bridge()->UnlockWindow(window);
    AndroidBridge::Bridge()->ReleaseNativeWindow(window);

    return surfaceBits;
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_onFullScreenPluginHidden(JNIEnv* jenv, jclass, jobject view)
{
  class ExitFullScreenRunnable : public nsRunnable {
    public:
      ExitFullScreenRunnable(jobject view) : mView(view) {}

      NS_IMETHODIMP Run() {
        JNIEnv* env = AndroidBridge::GetJNIEnv();
        if (!env) {
          NS_WARNING("Failed to acquire JNI env, can't exit plugin fullscreen mode");
          return NS_OK;
        }

        nsPluginInstanceOwner::ExitFullScreen(mView);
        env->DeleteGlobalRef(mView);
        return NS_OK;
      }

    private:
      jobject mView;
  };

  nsCOMPtr<nsIRunnable> runnable = new ExitFullScreenRunnable(jenv->NewGlobalRef(view));
  NS_DispatchToMainThread(runnable);
}

NS_EXPORT jobject JNICALL
Java_org_mozilla_gecko_GeckoAppShell_getNextMessageFromQueue(JNIEnv* jenv, jclass, jobject queue)
{
    static jclass jMessageQueueCls = nullptr;
    static jfieldID jMessagesField;
    static jmethodID jNextMethod;
    if (!jMessageQueueCls) {
        jMessageQueueCls = (jclass) jenv->NewGlobalRef(jenv->FindClass("android/os/MessageQueue"));
        jMessagesField = jenv->GetFieldID(jMessageQueueCls, "mMessages", "Landroid/os/Message;");
        jNextMethod = jenv->GetMethodID(jMessageQueueCls, "next", "()Landroid/os/Message;");
    }
    if (!jMessageQueueCls || !jMessagesField || !jNextMethod)
        return NULL;
    jobject msg = jenv->GetObjectField(queue, jMessagesField);
    // if queue.mMessages is null, queue.next() will block, which we don't want
    if (!msg)
        return msg;
    msg = jenv->CallObjectMethod(queue, jNextMethod);
    return msg;
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_onSurfaceTextureFrameAvailable(JNIEnv* jenv, jclass, jobject surfaceTexture, jint id)
{
  nsSurfaceTexture* st = nsSurfaceTexture::Find(id);
  if (!st) {
    __android_log_print(ANDROID_LOG_ERROR, "GeckoJNI", "Failed to find nsSurfaceTexture with id %d", id);
    return;
  }

  st->NotifyFrameAvailable();
}

#endif
}
