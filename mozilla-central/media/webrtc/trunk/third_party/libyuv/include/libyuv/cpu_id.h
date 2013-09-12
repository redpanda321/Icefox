/*
 *  Copyright 2011 The LibYuv Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef INCLUDE_LIBYUV_CPU_ID_H_  // NOLINT
#define INCLUDE_LIBYUV_CPU_ID_H_

#ifdef __cplusplus
namespace libyuv {
extern "C" {
#endif

// Internal flag to indicate cpuid is initialized.
static const int kCpuInitialized = 0x1;

// These flags are only valid on ARM processors.
static const int kCpuHasARM = 0x2;
static const int kCpuHasNEON = 0x4;
// 0x8 reserved for future ARM flag.

// These flags are only valid on x86 processors.
static const int kCpuHasX86 = 0x10;
static const int kCpuHasSSE2 = 0x20;
static const int kCpuHasSSSE3 = 0x40;
static const int kCpuHasSSE41 = 0x80;
static const int kCpuHasSSE42 = 0x100;
static const int kCpuHasAVX = 0x200;
// 0x400 reserved for AVX2.

// Detect CPU has SSE2 etc.
// Test_flag parameter should be one of kCpuHas constants above.
// returns non-zero if instruction set is detected
static __inline int TestCpuFlag(int test_flag) {
  extern int cpu_info_;
  extern int InitCpuFlags();
  return (cpu_info_ ? cpu_info_ : InitCpuFlags()) & test_flag;
}

// For testing, allow CPU flags to be disabled.
// ie MaskCpuFlags(~kCpuHasSSSE3) to disable SSSE3.
// MaskCpuFlags(-1) to enable all cpu specific optimizations.
// MaskCpuFlags(0) to disable all cpu specific optimizations.
void MaskCpuFlags(int enable_flags);

// Low level cpuid for X86.  Returns zeros on other CPUs.
void CpuId(int cpu_info[4], int info_type);

#ifdef __cplusplus
}  // extern "C"
}  // namespace libyuv
#endif

#endif  // INCLUDE_LIBYUV_CPU_ID_H_  NOLINT
