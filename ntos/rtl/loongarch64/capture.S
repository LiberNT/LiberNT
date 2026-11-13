/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 call-time supervisor context capture
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * CONTEXT is the one in sdk/include/xdk/loongarch64/ketypes.h: the whole 32
 * entry GPR bank (which carries PC through its own field, so the union holds
 * Ra, Tp, Sp, A0-A7, T0-T8, R21, Fp and S0-S8 as well), the 32 entry FPR bank
 * and Fcsr0. CxCONTEXT offsets come from asmoffsets.h, which asmframes.c
 * checks against the real structure.
 *
 * No scratch register is needed: $a0 is both the destination pointer and a
 * saved register, so every store uses it as the base while its own value is
 * still the argument the caller passed.
 *
 * Pc is the address this call returns to, i.e. $ra. CSR.ERA is not a
 * substitute: the manual defines it as the PC of the instruction that
 * triggered an exception, so outside exception handling it holds a stale
 * value ([V1-7.4.7]).
 */

#include "../../ke/loongarch64/asmoffsets.h"

#define LA64_CONTEXT_LOONGARCH64    0x01000000
#define LA64_CONTEXT_CONTROL        0x01000001
#define LA64_CONTEXT_INTEGER        0x01000002
#define LA64_CONTEXT_FP             0x01000004
#define LA64_CONTEXT_ALL            (LA64_CONTEXT_CONTROL | LA64_CONTEXT_INTEGER | LA64_CONTEXT_FP)

    .section .text$RtlCaptureContext, "ax", @progbits
    .balign 4
    .global RtlCaptureContext
    .type   RtlCaptureContext, @function
/* void RtlCaptureContext(PCONTEXT Context) */
RtlCaptureContext:
    st.d    $zero, $a0, CxR + 8 * 0
    st.d    $ra,   $a0, CxR + 8 * 1
    st.d    $tp,   $a0, CxR + 8 * 2
    st.d    $sp,   $a0, CxR + 8 * 3
    st.d    $a0,   $a0, CxR + 8 * 4
    st.d    $a1,   $a0, CxR + 8 * 5
    st.d    $a2,   $a0, CxR + 8 * 6
    st.d    $a3,   $a0, CxR + 8 * 7
    st.d    $a4,   $a0, CxR + 8 * 8
    st.d    $a5,   $a0, CxR + 8 * 9
    st.d    $a6,   $a0, CxR + 8 * 10
    st.d    $a7,   $a0, CxR + 8 * 11
    st.d    $t0,   $a0, CxR + 8 * 12
    st.d    $t1,   $a0, CxR + 8 * 13
    st.d    $t2,   $a0, CxR + 8 * 14
    st.d    $t3,   $a0, CxR + 8 * 15
    st.d    $t4,   $a0, CxR + 8 * 16
    st.d    $t5,   $a0, CxR + 8 * 17
    st.d    $t6,   $a0, CxR + 8 * 18
    st.d    $t7,   $a0, CxR + 8 * 19
    st.d    $t8,   $a0, CxR + 8 * 20
    st.d    $r21,  $a0, CxR + 8 * 21
    st.d    $fp,   $a0, CxR + 8 * 22
    st.d    $s0,   $a0, CxR + 8 * 23
    st.d    $s1,   $a0, CxR + 8 * 24
    st.d    $s2,   $a0, CxR + 8 * 25
    st.d    $s3,   $a0, CxR + 8 * 26
    st.d    $s4,   $a0, CxR + 8 * 27
    st.d    $s5,   $a0, CxR + 8 * 28
    st.d    $s6,   $a0, CxR + 8 * 29
    st.d    $s7,   $a0, CxR + 8 * 30
    st.d    $s8,   $a0, CxR + 8 * 31

    /* The return address of this call is the PC this frame resumes at. */
    st.d    $ra,   $a0, CxPc
    st.d    $zero, $a0, CxPc + 8

    /* EUEN.FPE gates the FPU: with it clear an fst.d would itself take an
     * FPD exception here, so an unavailable bank is zeroed and not advertised
     * in ContextFlags ([V1-7.4.3]). */
    csrrd   $t0, LA64_CSR_EUEN
    andi    $t0, $t0, LA64_EUEN_FPE
    beqz    $t0, .Lcapture_no_fp

    fst.d   $f0,  $a0, CxF + 8 * 0
    fst.d   $f1,  $a0, CxF + 8 * 1
    fst.d   $f2,  $a0, CxF + 8 * 2
    fst.d   $f3,  $a0, CxF + 8 * 3
    fst.d   $f4,  $a0, CxF + 8 * 4
    fst.d   $f5,  $a0, CxF + 8 * 5
    fst.d   $f6,  $a0, CxF + 8 * 6
    fst.d   $f7,  $a0, CxF + 8 * 7
    fst.d   $f8,  $a0, CxF + 8 * 8
    fst.d   $f9,  $a0, CxF + 8 * 9
    fst.d   $f10, $a0, CxF + 8 * 10
    fst.d   $f11, $a0, CxF + 8 * 11
    fst.d   $f12, $a0, CxF + 8 * 12
    fst.d   $f13, $a0, CxF + 8 * 13
    fst.d   $f14, $a0, CxF + 8 * 14
    fst.d   $f15, $a0, CxF + 8 * 15
    fst.d   $f16, $a0, CxF + 8 * 16
    fst.d   $f17, $a0, CxF + 8 * 17
    fst.d   $f18, $a0, CxF + 8 * 18
    fst.d   $f19, $a0, CxF + 8 * 19
    fst.d   $f20, $a0, CxF + 8 * 20
    fst.d   $f21, $a0, CxF + 8 * 21
    fst.d   $f22, $a0, CxF + 8 * 22
    fst.d   $f23, $a0, CxF + 8 * 23
    fst.d   $f24, $a0, CxF + 8 * 24
    fst.d   $f25, $a0, CxF + 8 * 25
    fst.d   $f26, $a0, CxF + 8 * 26
    fst.d   $f27, $a0, CxF + 8 * 27
    fst.d   $f28, $a0, CxF + 8 * 28
    fst.d   $f29, $a0, CxF + 8 * 29
    fst.d   $f30, $a0, CxF + 8 * 30
    fst.d   $f31, $a0, CxF + 8 * 31

    /* fcsr0 has no CSR number; it is reached with movfcsr2gr, [V1-3.2.4.1].
     * Fcc is not part of this capture: the trap prologue does not record it
     * either, so no context on this port advertises it. */
    movfcsr2gr $t0, $fcsr0
    st.w    $t0, $a0, CxFcsr
    li.d    $t0, LA64_CONTEXT_ALL
    st.w    $t0, $a0, CxContextFlags
    jr      $ra

.Lcapture_no_fp:
    st.w    $zero, $a0, CxFcsr
    li.d    $t0, (LA64_CONTEXT_CONTROL | LA64_CONTEXT_INTEGER)
    st.w    $t0, $a0, CxContextFlags
    jr      $ra
    .size   RtlCaptureContext, . - RtlCaptureContext

    .section .text$DbgBreakPoint, "ax", @progbits
    .balign 4
    .global DbgBreakPoint
    .type   DbgBreakPoint, @function
DbgBreakPoint:
    break   0
    jr      $ra
    .size   DbgBreakPoint, . - DbgBreakPoint

    .section .text$DbgBreakPointWithStatus, "ax", @progbits
    .balign 4
    .global DbgBreakPointWithStatus
    .global RtlpBreakWithStatusInstruction
    .type   DbgBreakPointWithStatus, @function
/* void DbgBreakPointWithStatus(ULONG Status)
 *
 * The status stays in $a0, which is where the interrupted register bank keeps
 * it for the debugger; this routine defines no KD packet of its own. */
DbgBreakPointWithStatus:
RtlpBreakWithStatusInstruction:
    break   0
    jr      $ra
    .size   DbgBreakPointWithStatus, . - DbgBreakPointWithStatus

/* EOF */
