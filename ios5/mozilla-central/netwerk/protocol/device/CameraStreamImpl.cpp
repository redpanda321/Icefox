/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CameraStreamImpl.h"
#include "nsCRTGlue.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"
#include "mozilla/Monitor.h"

/**
 * JNI part & helper runnable
 */

extern "C" {
    NS_EXPORT void JNICALL Java_org_mozilla_gecko_GeckoAppShell_cameraCallbackBridge(JNIEnv *, jclass, jbyteArray data);
}

NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_cameraCallbackBridge(JNIEnv *env, jclass, jbyteArray data) {
    mozilla::net::CameraStreamImpl* impl = mozilla::net::CameraStreamImpl::GetInstance(0);
    
    impl->transmitFrame(env, &data);
}

using namespace mozilla;

namespace mozilla {
namespace net {

static CameraStreamImpl* mCamera0 = NULL;
static CameraStreamImpl* mCamera1 = NULL;

/**
 * CameraStreamImpl
 */

void CameraStreamImpl::transmitFrame(JNIEnv *env, jbyteArray *data) {
    if (!mCallback)
      return;
    jboolean isCopy;
    jbyte* jFrame = env->GetByteArrayElements(*data, &isCopy);
    PRUint32 length = env->GetArrayLength(*data);
    if (length > 0) {
        mCallback->ReceiveFrame((char*)jFrame, length);
    }
    env->ReleaseByteArrayElements(*data, jFrame, 0);
}

CameraStreamImpl* CameraStreamImpl::GetInstance(PRUint32 aCamera) {
    CameraStreamImpl* res = NULL;
    switch(aCamera) {
        case 0:
            if (mCamera0)
                res = mCamera0;
            else
                res = mCamera0 = new CameraStreamImpl(aCamera);
            break;
        case 1:
            if (mCamera1)
                res = mCamera1;
            else
                res = mCamera1 = new CameraStreamImpl(aCamera);
            break;
    }
    return res;
}


CameraStreamImpl::CameraStreamImpl(PRUint32 aCamera) :
 mInit(false), mCamera(aCamera)
{
    NS_WARNING("CameraStreamImpl::CameraStreamImpl()");
    mWidth = 0;
    mHeight = 0;
    mFps = 0;
}

CameraStreamImpl::~CameraStreamImpl()
{
    NS_WARNING("CameraStreamImpl::~CameraStreamImpl()");
}

bool CameraStreamImpl::Init(const nsCString& contentType, const PRUint32& camera, const PRUint32& width, const PRUint32& height, FrameCallback* aCallback)
{
    mCallback = aCallback;
    mWidth = width;
    mHeight = height;
    return AndroidBridge::Bridge()->InitCamera(contentType, camera, &mWidth, &mHeight, &mFps);
}

void CameraStreamImpl::Close() {
    AndroidBridge::Bridge()->CloseCamera();
    mCallback = NULL;
}

} // namespace net
} // namespace mozilla
