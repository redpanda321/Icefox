/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/ImageContainerParent.h"
#include "mozilla/layers/ImageBridgeParent.h"
#include "mozilla/layers/SharedImageUtils.h"
#include "CompositorParent.h"

namespace mozilla {
namespace layers {

ImageContainerParent::ImageContainerParent(uint32_t aHandle)
: mID(aHandle), mStop(false) {
  MOZ_COUNT_CTOR(ImageContainerParent);
}

bool ImageContainerParent::RecvPublishImage(const SharedImage& aImage)
{
  SharedImage *copy = new SharedImage(aImage);
  SharedImage *prevImage = SwapSharedImage(mID, copy);

  uint32_t compositorID = GetCompositorIDForImage(mID);
  CompositorParent* compositor = CompositorParent::GetCompositor(compositorID);

  if (compositor) {
    compositor->ScheduleComposition();
  }

  if (prevImage && !mStop) {
    SendReturnImage(*prevImage);
    delete prevImage;
  }
  return true;
}

bool ImageContainerParent::RecvFlush()
{
  SharedImage *img = RemoveSharedImage(mID);
  if (img) {
    DeallocSharedImageData(this, *img);
    delete img;
  }
  return true;
}

void ImageContainerParent::DoStop()
{
  mStop = true;
}

bool ImageContainerParent::RecvStop()
{
  DoStop();
  return true;
}

bool ImageContainerParent::Recv__delete__()
{
  NS_ABORT_IF_FALSE(mStop, "Should be in a stopped state when __delete__");
  SharedImage* removed = RemoveSharedImage(mID);
  if (removed) {
    DeallocSharedImageData(this, *removed);
    delete removed;
  }

  return true;
}

ImageContainerParent::~ImageContainerParent()
{
  MOZ_COUNT_DTOR(ImageContainerParent);
  // On emergency shutdown, Recv__delete__ won't be invoked, so
  // we need to cleanup the global table here and not worry about
  // deallocating the shmem in the scenario since the emergency 
  // shutdown procedure takes care of that. 
  // On regular shutdown, Recv__delete__ also calls RemoveSharedImage
  // but it is not a problem because it is safe to call twice.
  SharedImage* removed = RemoveSharedImage(mID);
  if (removed) {
    delete removed;
  }
}

struct ImageIDPair {
  ImageIDPair(SharedImage* aImage, uint32_t aID)
  : image(aImage), id(aID), compositorID(0), version(1) {}
  SharedImage*  image;
  uint64_t      id;
  uint64_t      compositorID;
  uint32_t      version;
};

typedef nsTArray<ImageIDPair> SharedImageMap;
SharedImageMap *sSharedImageMap = nullptr;

static const int SHAREDIMAGEMAP_INVALID_INDEX = -1;

static int IndexOf(uint64_t aID)
{
  for (unsigned int i = 0; i < sSharedImageMap->Length(); ++i) {
    if ((*sSharedImageMap)[i].id == aID) {
      return i;
    }
  }
  return SHAREDIMAGEMAP_INVALID_INDEX;
}

bool ImageContainerParent::IsExistingID(uint64_t aID)
{
  return IndexOf(aID) != SHAREDIMAGEMAP_INVALID_INDEX;
}

SharedImage* ImageContainerParent::SwapSharedImage(uint64_t aID, 
                                                   SharedImage* aImage)
{
  int idx = IndexOf(aID);
  if (idx == SHAREDIMAGEMAP_INVALID_INDEX) {
    sSharedImageMap->AppendElement(ImageIDPair(aImage,aID));
    return nullptr;
  }
  SharedImage *prev = (*sSharedImageMap)[idx].image;
  (*sSharedImageMap)[idx].image = aImage;
  (*sSharedImageMap)[idx].version++;
  return prev;
}

uint32_t ImageContainerParent::GetSharedImageVersion(uint64_t aID)
{
  int idx = IndexOf(aID);
  if (idx == SHAREDIMAGEMAP_INVALID_INDEX) return 0;
  return (*sSharedImageMap)[idx].version;
}

SharedImage* ImageContainerParent::RemoveSharedImage(uint64_t aID) 
{
  int idx = IndexOf(aID);
  if (idx != SHAREDIMAGEMAP_INVALID_INDEX) {
    SharedImage* img = (*sSharedImageMap)[idx].image;
    sSharedImageMap->RemoveElementAt(idx);
    return img;
  }
  return nullptr;
}

SharedImage* ImageContainerParent::GetSharedImage(uint64_t aID)
{
  int idx = IndexOf(aID);
  if (idx != SHAREDIMAGEMAP_INVALID_INDEX) {
    return (*sSharedImageMap)[idx].image;
  }
  return nullptr;
}

bool ImageContainerParent::SetCompositorIDForImage(uint64_t aImageID, uint64_t aCompositorID)
{
  int idx = IndexOf(aImageID);
  if (idx == SHAREDIMAGEMAP_INVALID_INDEX) {
    return false;
  }
  (*sSharedImageMap)[idx].compositorID = aCompositorID;
  return true;
}

uint64_t ImageContainerParent::GetCompositorIDForImage(uint64_t aImageID)
{
  int idx = IndexOf(aImageID);
  if (idx != SHAREDIMAGEMAP_INVALID_INDEX) {
    return (*sSharedImageMap)[idx].compositorID;
  }
  return 0;
}

void ImageContainerParent::CreateSharedImageMap()
{
  if (sSharedImageMap == nullptr) {
    sSharedImageMap = new SharedImageMap;
  }
}
void ImageContainerParent::DestroySharedImageMap()
{
  if (sSharedImageMap != nullptr) {
    NS_ABORT_IF_FALSE(sSharedImageMap->Length() == 0,
                      "The global shared image map should be empty!");
    delete sSharedImageMap;
    sSharedImageMap = nullptr;
  }
}

} // namespace
} // namespace
