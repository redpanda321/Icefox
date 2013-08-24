/*
 *  Copyright (c) 2010 The VP8 project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license 
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may 
 *  be found in the AUTHORS file in the root of the source tree.
 */


#ifndef __INC_IDCT_H
#define __INC_IDCT_H

#define prototype_second_order(sym) \
    void sym(short *input, short *output)

#define prototype_idct(sym) \
    void sym(short *input, short *output, int pitch)

#define prototype_idct_scalar(sym) \
    void sym(short input, short *output, int pitch)

#if ARCH_X86 || ARCH_X86_64
#include "x86/idct_x86.h"
#endif

#if ARCH_ARM
#include "arm/idct_arm.h"
#endif

#ifndef vp8_idct_idct1
#define vp8_idct_idct1 vp8_short_idct4x4llm_1_c
#endif
extern prototype_idct(vp8_idct_idct1);

#ifndef vp8_idct_idct16
#define vp8_idct_idct16 vp8_short_idct4x4llm_c
#endif
extern prototype_idct(vp8_idct_idct16);

#ifndef vp8_idct_idct1_scalar
#define vp8_idct_idct1_scalar vp8_dc_only_idct_c
#endif
extern prototype_idct_scalar(vp8_idct_idct1_scalar);


#ifndef vp8_idct_iwalsh1
#define vp8_idct_iwalsh1 vp8_short_inv_walsh4x4_1_c
#endif
extern prototype_second_order(vp8_idct_iwalsh1);

#ifndef vp8_idct_iwalsh16
#define vp8_idct_iwalsh16 vp8_short_inv_walsh4x4_c
#endif
extern prototype_second_order(vp8_idct_iwalsh16);

typedef prototype_idct((*vp8_idct_fn_t));
typedef prototype_idct_scalar((*vp8_idct_scalar_fn_t));
typedef prototype_second_order((*vp8_second_order_fn_t));

typedef struct
{
    vp8_idct_fn_t         idct1;
    vp8_idct_fn_t         idct16;
    vp8_idct_scalar_fn_t  idct1_scalar;

    vp8_second_order_fn_t iwalsh1;
    vp8_second_order_fn_t iwalsh16;
} vp8_idct_rtcd_vtable_t;

#if CONFIG_RUNTIME_CPU_DETECT
#define IDCT_INVOKE(ctx,fn) (ctx)->fn
#else
#define IDCT_INVOKE(ctx,fn) vp8_idct_##fn
#endif

#endif
