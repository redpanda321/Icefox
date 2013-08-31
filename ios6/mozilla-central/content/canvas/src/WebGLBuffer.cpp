/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLBuffer.h"
#include "WebGLContext.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"

using namespace mozilla;

WebGLBuffer::WebGLBuffer(WebGLContext *context)
    : WebGLContextBoundObject(context)
    , mHasEverBeenBound(false)
    , mByteLength(0)
    , mTarget(LOCAL_GL_NONE)
{
    SetIsDOMBinding();
    mContext->MakeContextCurrent();
    mContext->gl->fGenBuffers(1, &mGLName);
    mContext->mBuffers.insertBack(this);
}

WebGLBuffer::~WebGLBuffer() {
    DeleteOnce();
}

void
WebGLBuffer::Delete() {
    mContext->MakeContextCurrent();
    mContext->gl->fDeleteBuffers(1, &mGLName);
    mByteLength = 0;
    mCache = nullptr;
    LinkedListElement<WebGLBuffer>::remove(); // remove from mContext->mBuffers
}

void
WebGLBuffer::SetTarget(GLenum target) {
    mTarget = target;
    if (!mCache && mTarget == LOCAL_GL_ELEMENT_ARRAY_BUFFER)
        mCache = new WebGLElementArrayCache;
}

bool
WebGLBuffer::ElementArrayCacheBufferData(const void* ptr, size_t buffer_size_in_bytes) {
    if (mTarget == LOCAL_GL_ELEMENT_ARRAY_BUFFER)
        return mCache->BufferData(ptr, buffer_size_in_bytes);
    return true;
}

void
WebGLBuffer::ElementArrayCacheBufferSubData(size_t pos, const void* ptr, size_t update_size_in_bytes) {
    if (mTarget == LOCAL_GL_ELEMENT_ARRAY_BUFFER)
        mCache->BufferSubData(pos, ptr, update_size_in_bytes);
}

JSObject*
WebGLBuffer::WrapObject(JSContext *cx, JSObject *scope, bool *triedToWrap) {
    return dom::WebGLBufferBinding::Wrap(cx, scope, this, triedToWrap);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_0(WebGLBuffer)

NS_IMPL_CYCLE_COLLECTING_ADDREF(WebGLBuffer)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WebGLBuffer)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebGLBuffer)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END
