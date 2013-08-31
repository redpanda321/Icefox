/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioBufferSourceNode.h"
#include "mozilla/dom/AudioBufferSourceNodeBinding.h"

namespace mozilla {
namespace dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED_1(AudioBufferSourceNode, AudioSourceNode, mBuffer)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(AudioBufferSourceNode)
NS_INTERFACE_MAP_END_INHERITING(AudioSourceNode)

NS_IMPL_ADDREF_INHERITED(AudioBufferSourceNode, AudioSourceNode)
NS_IMPL_RELEASE_INHERITED(AudioBufferSourceNode, AudioSourceNode)

AudioBufferSourceNode::AudioBufferSourceNode(AudioContext* aContext)
  : AudioSourceNode(aContext)
{
}

JSObject*
AudioBufferSourceNode::WrapObject(JSContext* aCx, JSObject* aScope,
                                  bool* aTriedToWrap)
{
  return AudioBufferSourceNodeBinding::Wrap(aCx, aScope, this, aTriedToWrap);
}

void
AudioBufferSourceNode::SetBuffer(AudioBuffer* aBuffer)
{
  mBuffer = aBuffer;
}

}
}

