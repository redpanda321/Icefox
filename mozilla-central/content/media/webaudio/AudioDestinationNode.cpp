/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioDestinationNode.h"
#include "mozilla/dom/AudioDestinationNodeBinding.h"

namespace mozilla {
namespace dom {

NS_IMPL_ISUPPORTS_INHERITED0(AudioDestinationNode, AudioNode)

AudioDestinationNode::AudioDestinationNode(AudioContext* aContext)
  : AudioNode(aContext)
{
}

JSObject*
AudioDestinationNode::WrapObject(JSContext* aCx, JSObject* aScope,
                                 bool* aTriedToWrap)
{
  return AudioDestinationNodeBinding::Wrap(aCx, aScope, this, aTriedToWrap);
}

}
}

