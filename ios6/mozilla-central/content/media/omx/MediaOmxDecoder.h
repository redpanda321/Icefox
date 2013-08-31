/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#if !defined(MediaOmxDecoder_h_)
#define MediaOmxDecoder_h_

#include "base/basictypes.h"
#include "MediaDecoder.h"

namespace mozilla {

class MediaOmxDecoder : public MediaDecoder
{
public:
  MediaOmxDecoder();
  ~MediaOmxDecoder();

  virtual MediaDecoder* Clone();
  virtual MediaDecoderStateMachine* CreateStateMachine();
};

} // namespace mozilla

#endif
