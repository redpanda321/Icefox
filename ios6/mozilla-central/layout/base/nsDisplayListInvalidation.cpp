/*-*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDisplayListInvalidation.h"
#include "nsDisplayList.h"

nsDisplayItemGeometry::nsDisplayItemGeometry(nsDisplayItem* aItem, nsDisplayListBuilder* aBuilder)
{
  MOZ_COUNT_CTOR(nsDisplayItemGeometry);
  bool snap;
  mBounds = aItem->GetBounds(aBuilder, &snap);
}

nsDisplayItemGeometry::~nsDisplayItemGeometry()
{
  MOZ_COUNT_DTOR(nsDisplayItemGeometry);
}

nsDisplayItemGenericGeometry::nsDisplayItemGenericGeometry(nsDisplayItem* aItem, nsDisplayListBuilder* aBuilder)
  : nsDisplayItemGeometry(aItem, aBuilder)
  , mBorderRect(aItem->GetBorderRect())
{}

void
nsDisplayItemGenericGeometry::MoveBy(const nsPoint& aOffset)
{
  mBounds.MoveBy(aOffset);
  mBorderRect.MoveBy(aOffset);
}

nsDisplayBorderGeometry::nsDisplayBorderGeometry(nsDisplayItem* aItem, nsDisplayListBuilder* aBuilder)
  : nsDisplayItemGeometry(aItem, aBuilder)
  , mContentRect(aItem->GetContentRect())
{}

void
nsDisplayBorderGeometry::MoveBy(const nsPoint& aOffset)
{
  mBounds.MoveBy(aOffset);
  mContentRect.MoveBy(aOffset);
}

nsDisplayBackgroundGeometry::nsDisplayBackgroundGeometry(nsDisplayBackgroundImage* aItem,
                                                         nsDisplayListBuilder* aBuilder)
  : nsDisplayItemGeometry(aItem, aBuilder)
  , mPositioningArea(aItem->GetPositioningArea())
{}

void
nsDisplayBackgroundGeometry::MoveBy(const nsPoint& aOffset)
{
  mBounds.MoveBy(aOffset);
  mPositioningArea.MoveBy(aOffset);
}

nsDisplayBoxShadowInnerGeometry::nsDisplayBoxShadowInnerGeometry(nsDisplayItem* aItem, nsDisplayListBuilder* aBuilder)
  : nsDisplayItemGeometry(aItem, aBuilder)
  , mPaddingRect(aItem->GetPaddingRect())
{}

void
nsDisplayBoxShadowInnerGeometry::MoveBy(const nsPoint& aOffset)
{
  mBounds.MoveBy(aOffset);
  mPaddingRect.MoveBy(aOffset);
}

