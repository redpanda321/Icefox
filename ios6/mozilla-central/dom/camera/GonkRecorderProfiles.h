/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_CAMERA_GONK_RECORDER_PROFILES_H
#define DOM_CAMERA_GONK_RECORDER_PROFILES_H

#include <media/MediaProfiles.h>
#include "CameraRecorderProfiles.h"

#ifndef CHECK_SETARG
#define CHECK_SETARG(x)                 \
  do {                                  \
    if (x) {                            \
      DOM_CAMERA_LOGE(#x " failed\n");  \
      return NS_ERROR_INVALID_ARG;      \
    }                                   \
  } while(0)
#endif


namespace android {
class GonkRecorder;
};

namespace mozilla {

/**
 * Gonk-specific video profile.
 */
class GonkRecorderVideoProfile : public RecorderVideoProfile
{
public:
  GonkRecorderVideoProfile(uint32_t aCameraId, uint32_t aQualityIndex);
  ~GonkRecorderVideoProfile();
  android::video_encoder GetPlatformCodec() const { return mPlatformCodec; }

protected:
  android::video_encoder mPlatformCodec;
};

/**
 * Gonk-specific audio profile.
 */
class GonkRecorderAudioProfile : public RecorderAudioProfile
{
public:
  GonkRecorderAudioProfile(uint32_t aCameraId, uint32_t aQualityIndex);
  ~GonkRecorderAudioProfile();
  android::audio_encoder GetPlatformCodec() const { return mPlatformCodec; }

protected:
  android::audio_encoder mPlatformCodec;
};

/**
 * Gonk-specific recorder profile.
 */
class GonkRecorderProfile : public RecorderProfileBase<GonkRecorderAudioProfile, GonkRecorderVideoProfile>
{
public:
  GonkRecorderProfile(uint32_t aCameraId, uint32_t aQualityIndex);

  GonkRecorderAudioProfile* GetGonkAudioProfile() { return &mAudio; }
  GonkRecorderVideoProfile* GetGonkVideoProfile() { return &mVideo; }

  android::output_format GetOutputFormat() const { return mPlatformOutputFormat; }
  nsresult ConfigureRecorder(android::GonkRecorder* aRecorder);

protected:
  virtual ~GonkRecorderProfile();

  android::output_format mPlatformOutputFormat;
};

/**
 * Gonk-specific profile manager.
 */
class GonkRecorderProfileManager : public RecorderProfileManager
{
public:
  GonkRecorderProfileManager(uint32_t aCameraId);

  /**
   * Call this function to indicate that the specified resolutions are in fact
   * supported by the camera hardware.  (Just because it appears in a recorder
   * profile doesn't mean the hardware can handle it.)
   */
  void SetSupportedResolutions(const nsTArray<dom::CameraSize>& aSizes)
    { mSupportedSizes = aSizes; }

  /**
   * Call this function to remove all resolutions set by calling
   * SetSupportedResolutions().
   */
  void ClearSupportedResolutions() { mSupportedSizes.Clear(); }

  bool IsSupported(uint32_t aQualityIndex) const;

  already_AddRefed<RecorderProfile> Get(uint32_t aQualityIndex) const;
  already_AddRefed<GonkRecorderProfile> Get(const char* aProfileName) const;
  nsresult ConfigureRecorder(android::GonkRecorder* aRecorder);

protected:
  virtual ~GonkRecorderProfileManager();

  nsTArray<dom::CameraSize> mSupportedSizes;
};

}; // namespace mozilla

#endif // DOM_CAMERA_GONK_RECORDER_PROFILES_H
