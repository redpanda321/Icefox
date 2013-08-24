;
;  Copyright (c) 2010 The VP8 project authors. All Rights Reserved.
;
;  Use of this source code is governed by a BSD-style license 
;  that can be found in the LICENSE file in the root of the source
;  tree. An additional intellectual property rights grant can be found
;  in the file PATENTS.  All contributing project authors may 
;  be found in the AUTHORS file in the root of the source tree.
;


%include "vpx_ports/x86_abi_support.asm"

; /****************************************************************************
; * Notes:
; *
; * This implementation makes use of 16 bit fixed point verio of two multiply
; * constants:
; *        1.   sqrt(2) * cos (pi/8)
; *         2.   sqrt(2) * sin (pi/8)
; * Becuase the first constant is bigger than 1, to maintain the same 16 bit
; * fixed point prrcision as the second one, we use a trick of
; *        x * a = x + x*(a-1)
; * so
; *        x * sqrt(2) * cos (pi/8) = x + x * (sqrt(2) *cos(pi/8)-1).
; *
; * For     the second constant, becuase of the 16bit version is 35468, which
; * is bigger than 32768, in signed 16 bit multiply, it become a negative
; * number.
; *        (x * (unsigned)35468 >> 16) = x * (signed)35468 >> 16 + x
; *
; **************************************************************************/


;void short_idct4x4llm_mmx(short *input, short *output, int pitch)
global sym(vp8_short_idct4x4llm_mmx)
sym(vp8_short_idct4x4llm_mmx):
    push        rbp
    mov         rbp, rsp
    SHADOW_ARGS_TO_STACK 3
    GET_GOT     rbx
    ; end prolog

        mov         rax,            arg(0) ;input
        mov         rdx,            arg(1) ;output

        movq        mm0,            [rax   ]
        movq        mm1,            [rax+ 8]

        movq        mm2,            [rax+16]
        movq        mm3,            [rax+24]

        movsxd      rax,            dword ptr arg(2) ;pitch

        psubw       mm0,            mm2             ; b1= 0-2
        paddw       mm2,            mm2             ;

        movq        mm5,            mm1
        paddw       mm2,            mm0             ; a1 =0+2

        pmulhw      mm5,            [x_s1sqr2 GLOBAL]        ;
        paddw       mm5,            mm1             ; ip1 * sin(pi/8) * sqrt(2)

        movq        mm7,            mm3             ;
        pmulhw      mm7,            [x_c1sqr2less1 GLOBAL]    ;

        paddw       mm7,            mm3             ; ip3 * cos(pi/8) * sqrt(2)
        psubw       mm7,            mm5             ; c1

        movq        mm5,            mm1
        movq        mm4,            mm3

        pmulhw      mm5,            [x_c1sqr2less1 GLOBAL]
        paddw       mm5,            mm1

        pmulhw      mm3,            [x_s1sqr2 GLOBAL]
        paddw       mm3,            mm4

        paddw       mm3,            mm5             ; d1
        movq        mm6,            mm2             ; a1

        movq        mm4,            mm0             ; b1
        paddw       mm2,            mm3             ;0

        paddw       mm4,            mm7             ;1
        psubw       mm0,            mm7             ;2

        psubw       mm6,            mm3             ;3

        movq        mm1,            mm2             ; 03 02 01 00
        movq        mm3,            mm4             ; 23 22 21 20

        punpcklwd   mm1,            mm0             ; 11 01 10 00
        punpckhwd   mm2,            mm0             ; 13 03 12 02

        punpcklwd   mm3,            mm6             ; 31 21 30 20
        punpckhwd   mm4,            mm6             ; 33 23 32 22

        movq        mm0,            mm1             ; 11 01 10 00
        movq        mm5,            mm2             ; 13 03 12 02

        punpckldq   mm0,            mm3             ; 30 20 10 00
        punpckhdq   mm1,            mm3             ; 31 21 11 01

        punpckldq   mm2,            mm4             ; 32 22 12 02
        punpckhdq   mm5,            mm4             ; 33 23 13 03

        movq        mm3,            mm5             ; 33 23 13 03

        psubw       mm0,            mm2             ; b1= 0-2
        paddw       mm2,            mm2             ;

        movq        mm5,            mm1
        paddw       mm2,            mm0             ; a1 =0+2

        pmulhw      mm5,            [x_s1sqr2 GLOBAL]         ;
        paddw       mm5,            mm1             ; ip1 * sin(pi/8) * sqrt(2)

        movq        mm7,            mm3             ;
        pmulhw      mm7,            [x_c1sqr2less1 GLOBAL]    ;

        paddw       mm7,            mm3             ; ip3 * cos(pi/8) * sqrt(2)
        psubw       mm7,            mm5             ; c1

        movq        mm5,            mm1
        movq        mm4,            mm3

        pmulhw      mm5,            [x_c1sqr2less1 GLOBAL]
        paddw       mm5,            mm1

        pmulhw      mm3,            [x_s1sqr2 GLOBAL]
        paddw       mm3,            mm4

        paddw       mm3,            mm5             ; d1
        paddw       mm0,            [fours GLOBAL]

        paddw       mm2,            [fours GLOBAL]
        movq        mm6,            mm2             ; a1

        movq        mm4,            mm0             ; b1
        paddw       mm2,            mm3             ;0

        paddw       mm4,            mm7             ;1
        psubw       mm0,            mm7             ;2

        psubw       mm6,            mm3             ;3
        psraw       mm2,            3

        psraw       mm0,            3
        psraw       mm4,            3

        psraw       mm6,            3

        movq        mm1,            mm2             ; 03 02 01 00
        movq        mm3,            mm4             ; 23 22 21 20

        punpcklwd   mm1,            mm0             ; 11 01 10 00
        punpckhwd   mm2,            mm0             ; 13 03 12 02

        punpcklwd   mm3,            mm6             ; 31 21 30 20
        punpckhwd   mm4,            mm6             ; 33 23 32 22

        movq        mm0,            mm1             ; 11 01 10 00
        movq        mm5,            mm2             ; 13 03 12 02

        punpckldq   mm0,            mm3             ; 30 20 10 00
        punpckhdq   mm1,            mm3             ; 31 21 11 01

        punpckldq   mm2,            mm4             ; 32 22 12 02
        punpckhdq   mm5,            mm4             ; 33 23 13 03

        movq        [rdx],          mm0

        movq        [rdx+rax],      mm1
        movq        [rdx+rax*2],    mm2

        add         rdx,            rax
        movq        [rdx+rax*2],    mm5

    ; begin epilog
    RESTORE_GOT
    UNSHADOW_ARGS
    pop         rbp
    ret


;void short_idct4x4llm_1_mmx(short *input, short *output, int pitch)
global sym(vp8_short_idct4x4llm_1_mmx)
sym(vp8_short_idct4x4llm_1_mmx):
    push        rbp
    mov         rbp, rsp
    SHADOW_ARGS_TO_STACK 3
    GET_GOT     rbx
    ; end prolog

        mov         rax,            arg(0) ;input
        movd        mm0,            [rax]

        paddw       mm0,            [fours GLOBAL]
        mov         rdx,            arg(1) ;output

        psraw       mm0,            3
        movsxd      rax,            dword ptr arg(2) ;pitch

        punpcklwd   mm0,            mm0
        punpckldq   mm0,            mm0

        movq        [rdx],          mm0
        movq        [rdx+rax],      mm0

        movq        [rdx+rax*2],    mm0
        add         rdx,            rax

        movq        [rdx+rax*2],    mm0


    ; begin epilog
    RESTORE_GOT
    UNSHADOW_ARGS
    pop         rbp
    ret

;void dc_only_idct_mmx(short input_dc, short *output, int pitch)
global sym(vp8_dc_only_idct_mmx)
sym(vp8_dc_only_idct_mmx):
    push        rbp
    mov         rbp, rsp
    SHADOW_ARGS_TO_STACK 3
    GET_GOT     rbx
    ; end prolog

        movd        mm0,            arg(0) ;input_dc

        paddw       mm0,            [fours GLOBAL]
        mov         rdx,            arg(1) ;output

        psraw       mm0,            3
        movsxd      rax,            dword ptr arg(2) ;pitch

        punpcklwd   mm0,            mm0
        punpckldq   mm0,            mm0

        movq        [rdx],          mm0
        movq        [rdx+rax],      mm0

        movq        [rdx+rax*2],    mm0
        add         rdx,            rax

        movq        [rdx+rax*2],    mm0

    ; begin epilog
    RESTORE_GOT
    UNSHADOW_ARGS
    pop         rbp
    ret

SECTION_RODATA
align 16
x_s1sqr2:
    times 4 dw 0x8A8C
align 16
x_c1sqr2less1:
    times 4 dw 0x4E7B
align 16
fours:
    times 4 dw 0x0004
