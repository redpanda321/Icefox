/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=4 et sw=4 tw=80: */
/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "base/basictypes.h"
#include "GonkCameraHwMgr.h"
#include "mozilla/layers/ShadowLayerUtilsGralloc.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "GonkNativeWindow.h"
#include "nsDebug.h"

/**
 * DOM_CAMERA_LOGI() is enabled in debug builds, and turned on by setting
 * NSPR_LOG_MODULES=Camera:N environment variable, where N >= 3.
 *
 * CNW_LOGE() is always enabled.
 */
#define CNW_LOGD(...)   DOM_CAMERA_LOGI(__VA_ARGS__)
#define CNW_LOGE(...)   {(void)printf_stderr(__VA_ARGS__);}

using namespace android;
using namespace mozilla::layers;

GonkNativeWindow::GonkNativeWindow(GonkNativeWindowNewFrameCallback* aCallback)
  : mNewFrameCallback(aCallback)
{
    GonkNativeWindow::init();
}

GonkNativeWindow::GonkNativeWindow()
  : mNewFrameCallback(nullptr)
{
    GonkNativeWindow::init();
}

GonkNativeWindow::~GonkNativeWindow()
{
    freeAllBuffersLocked();
}

void GonkNativeWindow::abandon()
{
    Mutex::Autolock lock(mMutex);
    ++mGeneration;
    CNW_LOGD("abandon: new generation %d", mGeneration);
    freeAllBuffersLocked();
    mDequeueCondition.signal();
}

void GonkNativeWindow::init()
{
    // Initialize the ANativeWindow function pointers.
    ANativeWindow::setSwapInterval  = hook_setSwapInterval;
    ANativeWindow::dequeueBuffer    = hook_dequeueBuffer;
    ANativeWindow::cancelBuffer     = hook_cancelBuffer;
    ANativeWindow::lockBuffer       = hook_lockBuffer;
    ANativeWindow::queueBuffer      = hook_queueBuffer;
    ANativeWindow::query            = hook_query;
    ANativeWindow::perform          = hook_perform;

    mDefaultWidth = 0;
    mDefaultHeight = 0;
    mPixelFormat = 0;
    mUsage = 0;
    mTimestamp = NATIVE_WINDOW_TIMESTAMP_AUTO;
    mBufferCount = MIN_BUFFER_SLOTS;
    mFrameCounter = 0;
    mGeneration = 0;
}


int GonkNativeWindow::hook_setSwapInterval(ANativeWindow* window, int interval)
{
    GonkNativeWindow* c = getSelf(window);
    return c->setSwapInterval(interval);
}

int GonkNativeWindow::hook_dequeueBuffer(ANativeWindow* window,
        ANativeWindowBuffer** buffer)
{
    GonkNativeWindow* c = getSelf(window);
    return c->dequeueBuffer(buffer);
}

int GonkNativeWindow::hook_cancelBuffer(ANativeWindow* window,
        ANativeWindowBuffer* buffer)
{
    GonkNativeWindow* c = getSelf(window);
    return c->cancelBuffer(buffer);
}

int GonkNativeWindow::hook_lockBuffer(ANativeWindow* window,
        ANativeWindowBuffer* buffer)
{
    GonkNativeWindow* c = getSelf(window);
    return c->lockBuffer(buffer);
}

int GonkNativeWindow::hook_queueBuffer(ANativeWindow* window,
        ANativeWindowBuffer* buffer)
{
    GonkNativeWindow* c = getSelf(window);
    return c->queueBuffer(buffer);
}

int GonkNativeWindow::hook_query(const ANativeWindow* window,
                                int what, int* value)
{
    const GonkNativeWindow* c = getSelf(window);
    return c->query(what, value);
}

int GonkNativeWindow::hook_perform(ANativeWindow* window, int operation, ...)
{
    va_list args;
    va_start(args, operation);
    GonkNativeWindow* c = getSelf(window);
    return c->perform(operation, args);
}

void GonkNativeWindow::freeBufferLocked(int i)
{
    ImageBridgeChild *ibc = ImageBridgeChild::GetSingleton();
    if (mSlots[i].mGraphicBuffer != NULL) {
        // Don't destroy the gralloc buffer if it is still in the
        // video stream awaiting rendering.
        if (mSlots[i].mBufferState != BufferSlot::RENDERING) {
            ibc->DeallocSurfaceDescriptorGralloc(mSlots[i].mSurfaceDescriptor);
        }
        mSlots[i].mGraphicBuffer = NULL;
        mSlots[i].mBufferState = BufferSlot::FREE;
        mSlots[i].mFrameNumber = 0;
    }
}

void GonkNativeWindow::freeAllBuffersLocked()
{
    for (int i = 0; i < NUM_BUFFER_SLOTS; i++) {
        freeBufferLocked(i);
    }
}

int GonkNativeWindow::setBufferCount(int bufferCount) {
    CNW_LOGD("setBufferCount: count=%d", bufferCount);
    Mutex::Autolock lock(mMutex);

    if (bufferCount > NUM_BUFFER_SLOTS) {
        CNW_LOGE("setBufferCount: bufferCount larger than slots available");
        return BAD_VALUE;
    }

    // special-case, nothing to do
    if (bufferCount == mBufferCount) {
        return OK;
    }

    if (bufferCount < MIN_BUFFER_SLOTS) {
        CNW_LOGE("setBufferCount: requested buffer count (%d) is less than "
                "minimum (%d)", bufferCount, MIN_BUFFER_SLOTS);
        return BAD_VALUE;
    }

    // Error out if the user has dequeued buffers or sent buffers to
    // video stream
    for (int i=0 ; i<mBufferCount ; i++) {
        if (mSlots[i].mBufferState == BufferSlot::DEQUEUED ||
            mSlots[i].mBufferState == BufferSlot::RENDERING) {
            CNW_LOGE("setBufferCount: client owns some buffers");
            return -EINVAL;
        }
    }

    if (bufferCount > mBufferCount) {
        // easy, we just have more buffers
        mBufferCount = bufferCount;
        mDequeueCondition.signal();
        return OK;
    }

    // reducing the number of buffers
    // here we're guaranteed that the client doesn't have dequeued buffers
    // and will release all of its buffer references.
    freeAllBuffersLocked();
    mBufferCount = bufferCount;
    mDequeueCondition.signal();
    return OK;
}

int GonkNativeWindow::dequeueBuffer(android_native_buffer_t** buffer)
{
    Mutex::Autolock lock(mMutex);

    int found = -1;
    int dequeuedCount = 0;
    bool tryAgain = true;

    CNW_LOGD("dequeueBuffer: E");
    while (tryAgain) {
        // look for a free buffer to give to the client
        found = INVALID_BUFFER_SLOT;
        dequeuedCount = 0;
        for (int i = 0; i < mBufferCount; i++) {
            const int state = mSlots[i].mBufferState;
            if (state == BufferSlot::DEQUEUED) {
                dequeuedCount++;
            }
            else if (state == BufferSlot::FREE) {
                /* We return the oldest of the free buffers to avoid
                 * stalling the producer if possible.  This is because
                 * the consumer may still have pending reads of the
                 * buffers in flight.
                 */
                bool isOlder = mSlots[i].mFrameNumber < mSlots[found].mFrameNumber;
                if (found < 0 || isOlder) {
                    found = i;
                }
            }
        }

        // we're in synchronous mode and didn't find a buffer, we need to
        // wait for some buffers to be consumed
        tryAgain = (found == INVALID_BUFFER_SLOT);
        if (tryAgain) {
            CNW_LOGD("dequeueBuffer: Try again");
            mDequeueCondition.wait(mMutex);
            CNW_LOGD("dequeueBuffer: Now");
        }
    }

    if (found == INVALID_BUFFER_SLOT) {
        // This should not happen.
        CNW_LOGE("dequeueBuffer: no available buffer slots");
        return -EBUSY;
    }

    const int buf = found;

    // buffer is now in DEQUEUED
    mSlots[buf].mBufferState = BufferSlot::DEQUEUED;

    const sp<GraphicBuffer>& gbuf(mSlots[buf].mGraphicBuffer);

    if (gbuf == NULL) {
        status_t error;
        SurfaceDescriptor buffer;
        ImageBridgeChild *ibc = ImageBridgeChild::GetSingleton();
        ibc->AllocSurfaceDescriptorGralloc(gfxIntSize(mDefaultWidth, mDefaultHeight),
                                           mPixelFormat,
                                           mUsage,
                                           &buffer);
        // We can only use a gralloc buffer here.  If we didn't get
        // one back, something went wrong.
        if (SurfaceDescriptor::TSurfaceDescriptorGralloc != buffer.type()) {
            MOZ_ASSERT(SurfaceDescriptor::T__None == buffer.type());
            CNW_LOGE("dequeueBuffer: failed to alloc gralloc buffer");
            return -ENOMEM;
        }
        sp<GraphicBuffer> graphicBuffer =
          GrallocBufferActor::GetFrom(buffer.get_SurfaceDescriptorGralloc());
        error = graphicBuffer->initCheck();
        if (error != NO_ERROR) {
            CNW_LOGE("dequeueBuffer: createGraphicBuffer failed with error %d",error);
            return error;
        }
        mSlots[buf].mGraphicBuffer = graphicBuffer;
        mSlots[buf].mSurfaceDescriptor = buffer;
        mSlots[buf].mSurfaceDescriptor.get_SurfaceDescriptorGralloc().external() = true;
    }
    *buffer = mSlots[buf].mGraphicBuffer.get();

    CNW_LOGD("dequeueBuffer: returning slot=%d buf=%p ", buf,
            mSlots[buf].mGraphicBuffer->handle );

    CNW_LOGD("dequeueBuffer: X");
    return NO_ERROR;
}

int GonkNativeWindow::getSlotFromBufferLocked(
        android_native_buffer_t* buffer) const
{
    if (buffer == NULL) {
        CNW_LOGE("getSlotFromBufferLocked: encountered NULL buffer");
        return BAD_VALUE;
    }

    for (int i = 0; i < NUM_BUFFER_SLOTS; i++) {
        if (mSlots[i].mGraphicBuffer != NULL && mSlots[i].mGraphicBuffer->handle == buffer->handle) {
            return i;
        }
    }
    CNW_LOGE("getSlotFromBufferLocked: unknown buffer: %p", buffer->handle);
    return BAD_VALUE;
}

mozilla::layers::SurfaceDescriptor *
GonkNativeWindow::getSurfaceDescriptorFromBuffer(ANativeWindowBuffer* buffer)
{
  int buf = getSlotFromBufferLocked(buffer);
  if (buf < 0 || buf >= mBufferCount ||
      mSlots[buf].mBufferState != BufferSlot::DEQUEUED) {
    return nullptr;
  }

  return &mSlots[buf].mSurfaceDescriptor;
}

int GonkNativeWindow::queueBuffer(ANativeWindowBuffer* buffer)
{
    {
        Mutex::Autolock lock(mMutex);
        CNW_LOGD("queueBuffer: E");
        int buf = getSlotFromBufferLocked(buffer);

        if (buf < 0 || buf >= mBufferCount) {
            CNW_LOGE("queueBuffer: slot index out of range [0, %d]: %d",
                     mBufferCount, buf);
            return -EINVAL;
        } else if (mSlots[buf].mBufferState != BufferSlot::DEQUEUED) {
            CNW_LOGE("queueBuffer: slot %d is not owned by the client "
                     "(state=%d)", buf, mSlots[buf].mBufferState);
            return -EINVAL;
        }

        int64_t timestamp;
        if (mTimestamp == NATIVE_WINDOW_TIMESTAMP_AUTO) {
            timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
        } else {
            timestamp = mTimestamp;
        }

        mSlots[buf].mBufferState = BufferSlot::QUEUED;
        mSlots[buf].mTimestamp = timestamp;
        mFrameCounter++;
        mSlots[buf].mFrameNumber = mFrameCounter;

        mDequeueCondition.signal();
    }

    // OnNewFrame might call lockCurrentBuffer so we must release the
    // mutex first.
    if (mNewFrameCallback) {
      mNewFrameCallback->OnNewFrame();
    }
    CNW_LOGD("queueBuffer: X");
    return OK;
}


already_AddRefed<GraphicBufferLocked>
GonkNativeWindow::getCurrentBuffer()
{
  CNW_LOGD("GonkNativeWindow::getCurrentBuffer");
  Mutex::Autolock lock(mMutex);

  int found = -1;
  for (int i = 0; i < mBufferCount; i++) {
    const int state = mSlots[i].mBufferState;
    if (state == BufferSlot::QUEUED) {
      if (found < 0 ||
          mSlots[i].mFrameNumber < mSlots[found].mFrameNumber) {
        found = i;
      }
    }
  }

  if (found < 0) {
    mDequeueCondition.signal();
    return NULL;
  }

  mSlots[found].mBufferState = BufferSlot::RENDERING;

  nsRefPtr<GraphicBufferLocked> ret =
    new CameraGraphicBuffer(this, found, mGeneration, mSlots[found].mSurfaceDescriptor);
  mDequeueCondition.signal();
  return ret.forget();
}

bool
GonkNativeWindow::returnBuffer(uint32_t aIndex, uint32_t aGeneration)
{
  CNW_LOGD("GonkNativeWindow::returnBuffer: slot=%d (generation=%d)", aIndex, aGeneration);
  Mutex::Autolock lock(mMutex);

  if (aGeneration != mGeneration) {
    CNW_LOGD("returnBuffer: buffer is from generation %d (current is %d)",
      aGeneration, mGeneration);
    return false;
  }
  if (aIndex < 0 || aIndex >= mBufferCount) {
    CNW_LOGE("returnBuffer: slot index out of range [0, %d]: %d",
             mBufferCount, aIndex);
    return false;
  }
  if (mSlots[aIndex].mBufferState != BufferSlot::RENDERING) {
    CNW_LOGE("returnBuffer: slot %d is not owned by the compositor (state=%d)",
                  aIndex, mSlots[aIndex].mBufferState);
    return false;
  }

  mSlots[aIndex].mBufferState = BufferSlot::FREE;
  mDequeueCondition.signal();
  return true;
}

int GonkNativeWindow::lockBuffer(ANativeWindowBuffer* buffer)
{
    CNW_LOGD("GonkNativeWindow::lockBuffer");
    Mutex::Autolock lock(mMutex);
    return OK;
}

int GonkNativeWindow::cancelBuffer(ANativeWindowBuffer* buffer)
{
    Mutex::Autolock lock(mMutex);
    int buf = getSlotFromBufferLocked(buffer);

    CNW_LOGD("cancelBuffer: slot=%d", buf);
    if (buf < 0 || buf >= mBufferCount) {
        CNW_LOGE("cancelBuffer: slot index out of range [0, %d]: %d",
                mBufferCount, buf);
        return -EINVAL;
    } else if (mSlots[buf].mBufferState != BufferSlot::DEQUEUED) {
        CNW_LOGE("cancelBuffer: slot %d is not owned by the client (state=%d)",
                buf, mSlots[buf].mBufferState);
        return -EINVAL;
    }
    mSlots[buf].mBufferState = BufferSlot::FREE;
    mSlots[buf].mFrameNumber = 0;
    mDequeueCondition.signal();
    return OK;
}

int GonkNativeWindow::perform(int operation, va_list args)
{
    switch (operation) {
        case NATIVE_WINDOW_SET_BUFFERS_TRANSFORM:
        case NATIVE_WINDOW_SET_BUFFERS_SIZE:
        case NATIVE_WINDOW_SET_SCALING_MODE:
        case NATIVE_WINDOW_SET_CROP:
        case NATIVE_WINDOW_CONNECT:
        case NATIVE_WINDOW_DISCONNECT:
            // deprecated. must return NO_ERROR.
            return NO_ERROR;
        case NATIVE_WINDOW_SET_USAGE:
            return dispatchSetUsage(args);
        case NATIVE_WINDOW_SET_BUFFER_COUNT:
            return dispatchSetBufferCount(args);
        case NATIVE_WINDOW_SET_BUFFERS_GEOMETRY:
            return dispatchSetBuffersGeometry(args);
        case NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP:
            return dispatchSetBuffersTimestamp(args);
        case NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS:
            return dispatchSetBuffersDimensions(args);
        case NATIVE_WINDOW_SET_BUFFERS_FORMAT:
            return dispatchSetBuffersFormat(args);
        case NATIVE_WINDOW_LOCK:
        case NATIVE_WINDOW_UNLOCK_AND_POST:
        case NATIVE_WINDOW_API_CONNECT:
        case NATIVE_WINDOW_API_DISCONNECT:
        default:
            NS_WARNING("Unsupported operation");
            return INVALID_OPERATION;
    }
}

int GonkNativeWindow::query(int what, int* outValue) const
{
    Mutex::Autolock lock(mMutex);

    int value;
    switch (what) {
    case NATIVE_WINDOW_WIDTH:
        value = mDefaultWidth;
        break;
    case NATIVE_WINDOW_HEIGHT:
        value = mDefaultHeight;
        break;
    case NATIVE_WINDOW_FORMAT:
        value = mPixelFormat;
        break;
    case NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS:
        value = MIN_UNDEQUEUED_BUFFERS;
        break;
    default:
        return BAD_VALUE;
    }
    outValue[0] = value;
    return NO_ERROR;
}

int GonkNativeWindow::setSwapInterval(int interval)
{
    return NO_ERROR;
}

int GonkNativeWindow::dispatchSetUsage(va_list args)
{
    int usage = va_arg(args, int);
    return setUsage(usage);
}

int GonkNativeWindow::dispatchSetBufferCount(va_list args)
{
    size_t bufferCount = va_arg(args, size_t);
    return setBufferCount(bufferCount);
}

int GonkNativeWindow::dispatchSetBuffersGeometry(va_list args)
{
    int w = va_arg(args, int);
    int h = va_arg(args, int);
    int f = va_arg(args, int);
    int err = setBuffersDimensions(w, h);
    if (err != 0) {
        return err;
    }
    return setBuffersFormat(f);
}

int GonkNativeWindow::dispatchSetBuffersDimensions(va_list args)
{
    int w = va_arg(args, int);
    int h = va_arg(args, int);
    return setBuffersDimensions(w, h);
}

int GonkNativeWindow::dispatchSetBuffersFormat(va_list args)
{
    int f = va_arg(args, int);
    return setBuffersFormat(f);
}

int GonkNativeWindow::dispatchSetBuffersTimestamp(va_list args)
{
    int64_t timestamp = va_arg(args, int64_t);
    return setBuffersTimestamp(timestamp);
}

int GonkNativeWindow::setUsage(uint32_t reqUsage)
{
    CNW_LOGD("GonkNativeWindow::setUsage");
    Mutex::Autolock lock(mMutex);
    mUsage = reqUsage;
    return OK;
}

int GonkNativeWindow::setBuffersDimensions(int w, int h)
{
    CNW_LOGD("GonkNativeWindow::setBuffersDimensions");
    Mutex::Autolock lock(mMutex);

    if (w<0 || h<0)
        return BAD_VALUE;

    if ((w && !h) || (!w && h))
        return BAD_VALUE;

    mDefaultWidth = w;
    mDefaultHeight = h;

    return OK;
}

int GonkNativeWindow::setBuffersFormat(int format)
{
    CNW_LOGD("GonkNativeWindow::setBuffersFormat");
    Mutex::Autolock lock(mMutex);

    if (format<0)
        return BAD_VALUE;

    mPixelFormat = format;

    return NO_ERROR;
}

int GonkNativeWindow::setBuffersTimestamp(int64_t timestamp)
{
    CNW_LOGD("GonkNativeWindow::setBuffersTimestamp");
    Mutex::Autolock lock(mMutex);
    mTimestamp = timestamp;
    return OK;
}
