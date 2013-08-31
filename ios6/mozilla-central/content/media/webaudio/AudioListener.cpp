/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioListener.h"
#include "AudioContext.h"
#include "nsContentUtils.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/AudioListenerBinding.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_1(AudioListener, mContext)

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(AudioListener, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(AudioListener, Release)

AudioListener::AudioListener(AudioContext* aContext)
  : mContext(aContext)
  , mPosition()
  , mOrientation(0.f, 0.f, -1.f)
  , mUpVector(0.f, 1.f, 0.f)
  , mVelocity()
  , mDopplerFactor(1.f)
  , mSpeedOfSound(343.3f) // meters/second
{
  MOZ_ASSERT(aContext);
  SetIsDOMBinding();
}

JSObject*
AudioListener::WrapObject(JSContext* aCx, JSObject* aScope,
                          bool* aTriedToWrap)
{
  return AudioListenerBinding::Wrap(aCx, aScope, this, aTriedToWrap);
}

}
}

