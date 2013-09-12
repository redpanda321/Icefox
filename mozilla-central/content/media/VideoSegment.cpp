/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VideoSegment.h"
#include "ImageContainer.h"

namespace mozilla {

using namespace layers;

VideoFrame::VideoFrame(already_AddRefed<Image> aImage, const gfxIntSize& aIntrinsicSize)
  : mImage(aImage), mIntrinsicSize(aIntrinsicSize)
{}

VideoFrame::VideoFrame()
  : mIntrinsicSize(0, 0)
{}

VideoFrame::~VideoFrame()
{}

void
VideoFrame::SetNull() {
  mImage = nullptr;
  mIntrinsicSize = gfxIntSize(0, 0);
}

void
VideoFrame::TakeFrom(VideoFrame* aFrame)
{
  mImage = aFrame->mImage.forget();
  mIntrinsicSize = aFrame->mIntrinsicSize;
}

VideoChunk::VideoChunk()
{}

VideoChunk::~VideoChunk()
{}

void
VideoSegment::AppendFrame(already_AddRefed<Image> aImage, TrackTicks aDuration,
                          const gfxIntSize& aIntrinsicSize)
{
  VideoChunk* chunk = AppendChunk(aDuration);
  VideoFrame frame(aImage, aIntrinsicSize);
  chunk->mFrame.TakeFrom(&frame);
}

VideoSegment::VideoSegment()
  : MediaSegmentBase<VideoSegment, VideoChunk>(VIDEO)
{}

VideoSegment::~VideoSegment()
{}

}
