/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Preferences.h"
#include "mozilla/Likely.h"

#include "nsIHttpChannel.h"
#include "nsSimpleURI.h"

#include "RasterImage.h"
#include "VectorImage.h"

#include "ImageFactory.h"

namespace mozilla {
namespace image {

const char* SVG_MIMETYPE = "image/svg+xml";

// Global preferences related to image containers.
static bool gInitializedPrefCaches = false;
static bool gDecodeOnDraw = false;
static bool gDiscardable = false;

static void
InitPrefCaches()
{
  Preferences::AddBoolVarCache(&gDiscardable, "image.mem.discardable");
  Preferences::AddBoolVarCache(&gDecodeOnDraw, "image.mem.decodeondraw");
  gInitializedPrefCaches = true;
}

static uint32_t
ComputeImageFlags(nsIURI* uri, bool isMultiPart)
{
  nsresult rv;

  // We default to the static globals
  bool isDiscardable = gDiscardable;
  bool doDecodeOnDraw = gDecodeOnDraw;

  // We want UI to be as snappy as possible and not to flicker. Disable discarding
  // and decode-on-draw for chrome URLS
  bool isChrome = false;
  rv = uri->SchemeIs("chrome", &isChrome);
  if (NS_SUCCEEDED(rv) && isChrome)
    isDiscardable = doDecodeOnDraw = false;

  // We don't want resources like the "loading" icon to be discardable or
  // decode-on-draw either.
  bool isResource = false;
  rv = uri->SchemeIs("resource", &isResource);
  if (NS_SUCCEEDED(rv) && isResource)
    isDiscardable = doDecodeOnDraw = false;

  // For multipart/x-mixed-replace, we basically want a direct channel to the
  // decoder. Disable both for this case as well.
  if (isMultiPart)
    isDiscardable = doDecodeOnDraw = false;

  // We have all the information we need
  uint32_t imageFlags = Image::INIT_FLAG_NONE;
  if (isDiscardable)
    imageFlags |= Image::INIT_FLAG_DISCARDABLE;
  if (doDecodeOnDraw)
    imageFlags |= Image::INIT_FLAG_DECODE_ON_DRAW;
  if (isMultiPart)
    imageFlags |= Image::INIT_FLAG_MULTIPART;

  return imageFlags;
}

/* static */ already_AddRefed<Image>
ImageFactory::CreateImage(nsIRequest* aRequest,
                          imgStatusTracker* aStatusTracker,
                          const nsCString& aMimeType,
                          nsIURI* aURI,
                          bool aIsMultiPart,
                          uint32_t aInnerWindowId)
{
  // Register our pref observers if we haven't yet.
  if (MOZ_UNLIKELY(!gInitializedPrefCaches))
    InitPrefCaches();

  // Compute the image's initialization flags.
  uint32_t imageFlags = ComputeImageFlags(aURI, aIsMultiPart);

  // Select the type of image to create based on MIME type.
  if (aMimeType.Equals(SVG_MIMETYPE)) {
    return CreateVectorImage(aRequest, aStatusTracker, aMimeType,
                             aURI, imageFlags, aInnerWindowId);
  } else {
    return CreateRasterImage(aRequest, aStatusTracker, aMimeType,
                             aURI, imageFlags, aInnerWindowId);
  }
}

// Marks an image as having an error before returning it. Used with macros like
// NS_ENSURE_SUCCESS, since we guarantee to always return an image even if an
// error occurs, but callers need to be able to tell that this happened.
template <typename T>
static already_AddRefed<Image>
BadImage(nsRefPtr<T>& image)
{
  image->SetHasError();
  return image.forget();
}

/* static */ already_AddRefed<Image>
ImageFactory::CreateAnonymousImage(const nsCString& aMimeType)
{
  nsresult rv;

  nsRefPtr<RasterImage> newImage = new RasterImage();

  rv = newImage->Init(nullptr, aMimeType.get(), Image::INIT_FLAG_NONE);
  NS_ENSURE_SUCCESS(rv, BadImage(newImage));

  return newImage.forget();
}

/* static */ already_AddRefed<Image>

ImageFactory::CreateRasterImage(nsIRequest* aRequest,
                                imgStatusTracker* aStatusTracker,
                                const nsCString& aMimeType,
                                nsIURI* aURI,
                                uint32_t aImageFlags,
                                uint32_t aInnerWindowId)
{
  nsresult rv;

  nsRefPtr<RasterImage> newImage = new RasterImage(aStatusTracker, aURI);

  rv = newImage->Init(aStatusTracker->GetDecoderObserver(),
                      aMimeType.get(), aImageFlags);
  NS_ENSURE_SUCCESS(rv, BadImage(newImage));

  newImage->SetInnerWindowID(aInnerWindowId);

  // Use content-length as a size hint for http channels.
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(aRequest));
  if (httpChannel) {
    nsAutoCString contentLength;
    rv = httpChannel->GetResponseHeader(NS_LITERAL_CSTRING("content-length"),
                                        contentLength);
    if (NS_SUCCEEDED(rv)) {
      int32_t len = contentLength.ToInteger(&rv);

      // Pass anything usable on so that the RasterImage can preallocate
      // its source buffer
      if (len > 0) {
        uint32_t sizeHint = (uint32_t) len;
        sizeHint = NS_MIN<uint32_t>(sizeHint, 20000000); // Bound by something reasonable
        rv = newImage->SetSourceSizeHint(sizeHint);
        if (NS_FAILED(rv)) {
          // Flush memory, try to get some back, and try again
          rv = nsMemory::HeapMinimize(true);
          nsresult rv2 = newImage->SetSourceSizeHint(sizeHint);
          // If we've still failed at this point, things are going downhill
          if (NS_FAILED(rv) || NS_FAILED(rv2)) {
            NS_WARNING("About to hit OOM in imagelib!");
          }
        }
      }
    }
  }

  return newImage.forget();
}

/* static */ already_AddRefed<Image>
ImageFactory::CreateVectorImage(nsIRequest* aRequest,
                                imgStatusTracker* aStatusTracker,
                                const nsCString& aMimeType,
                                nsIURI* aURI,
                                uint32_t aImageFlags,
                                uint32_t aInnerWindowId)
{
  nsresult rv;

  nsRefPtr<VectorImage> newImage = new VectorImage(aStatusTracker, aURI);

  rv = newImage->Init(aStatusTracker->GetDecoderObserver(),
                      aMimeType.get(), aImageFlags);
  NS_ENSURE_SUCCESS(rv, BadImage(newImage));

  newImage->SetInnerWindowID(aInnerWindowId);

  rv = newImage->OnStartRequest(aRequest, nullptr);
  NS_ENSURE_SUCCESS(rv, BadImage(newImage));

  return newImage.forget();
}

} // namespace image
} // namespace mozilla
