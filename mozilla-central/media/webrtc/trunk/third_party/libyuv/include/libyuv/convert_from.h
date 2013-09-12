/*
 *  Copyright 2011 The LibYuv Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef INCLUDE_LIBYUV_CONVERT_FROM_H_  // NOLINT
#define INCLUDE_LIBYUV_CONVERT_FROM_H_

#include "libyuv/basic_types.h"
#include "libyuv/rotate.h"

#ifdef __cplusplus
namespace libyuv {
extern "C" {
#endif

// See Also convert.h for conversions from formats to I420

// I420Copy in convert to I420ToI420

int I420ToI422(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height);

int I420ToI444(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height);

int I420ToI411(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_y, int dst_stride_y,
               uint8* dst_u, int dst_stride_u,
               uint8* dst_v, int dst_stride_v,
               int width, int height);

// Copy to I400.  Source can be I420,422,444,400,NV12,NV21
int I400Copy(const uint8* src_y, int src_stride_y,
             uint8* dst_y, int dst_stride_y,
             int width, int height);

// TODO(fbarchard): I420ToNV12
// TODO(fbarchard): I420ToM420
// TODO(fbarchard): I420ToQ420

int I420ToYUY2(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_frame, int dst_stride_frame,
               int width, int height);

int I420ToUYVY(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_frame, int dst_stride_frame,
               int width, int height);

int I420ToV210(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_frame, int dst_stride_frame,
               int width, int height);

int I420ToARGB(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height);

int I420ToBGRA(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height);

int I420ToABGR(const uint8* src_y, int src_stride_y,
               const uint8* src_u, int src_stride_u,
               const uint8* src_v, int src_stride_v,
               uint8* dst_argb, int dst_stride_argb,
               int width, int height);

int I420ToRGB24(const uint8* src_y, int src_stride_y,
                const uint8* src_u, int src_stride_u,
                const uint8* src_v, int src_stride_v,
                uint8* dst_frame, int dst_stride_frame,
                int width, int height);

int I420ToRAW(const uint8* src_y, int src_stride_y,
              const uint8* src_u, int src_stride_u,
              const uint8* src_v, int src_stride_v,
              uint8* dst_frame, int dst_stride_frame,
              int width, int height);

int I420ToRGB565(const uint8* src_y, int src_stride_y,
                 const uint8* src_u, int src_stride_u,
                 const uint8* src_v, int src_stride_v,
                 uint8* dst_frame, int dst_stride_frame,
                 int width, int height);

int I420ToARGB1555(const uint8* src_y, int src_stride_y,
                   const uint8* src_u, int src_stride_u,
                   const uint8* src_v, int src_stride_v,
                   uint8* dst_frame, int dst_stride_frame,
                   int width, int height);

int I420ToARGB4444(const uint8* src_y, int src_stride_y,
                   const uint8* src_u, int src_stride_u,
                   const uint8* src_v, int src_stride_v,
                   uint8* dst_frame, int dst_stride_frame,
                   int width, int height);

// Note Bayer formats (BGGR) To I420 are in format_conversion.h

// Convert I420 to specified format.
// "dst_sample_stride" is bytes in a row for the destination. Pass 0 if the
//    buffer has contiguous rows. Can be negative. A multiple of 16 is optimal.
int ConvertFromI420(const uint8* y, int y_stride,
                    const uint8* u, int u_stride,
                    const uint8* v, int v_stride,
                    uint8* dst_sample, int dst_sample_stride,
                    int width, int height,
                    uint32 format);

#ifdef __cplusplus
}  // extern "C"
}  // namespace libyuv
#endif

#endif  // INCLUDE_LIBYUV_CONVERT_FROM_H_  NOLINT
