/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LoongArch64 user-mode context and breakpoint support
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* The generated <asm/ksloongarch64.h> is empty on this port (see the
 * explanation in ntos/ke/loongarch64/asmoffsets.h), so CONTEXT offsets and
 * CSR numbers come from the one place that carries them and proves them
 * against the real structures in asmframes.c. */
#include "../../../ntos/ke/loongarch64/asmoffsets.h"

#define LA64_CONTEXT_LOONGARCH64    0x01000000
#define LA64_CONTEXT_CONTROL        0x01000001
#define LA64_CONTEXT_INTEGER        0x01000002
#define LA64_CONTEXT_FP             0x01000004
#define LA64_CONTEXT_ALL            (LA64_CONTEXT_CONTROL | LA64_CONTEXT_INTEGER | LA64_CONTEXT_FP)

/* The private debugger service id the kernel dispatcher recognises in $a7;
 * it is far above any SSDT entry, so no real service can collide with it. */
#define LA64_DEBUG_SERVICE_ID       0xFFFFFFFD

    .text
    .balign 4
    .globl RtlCaptureContext
    .type   RtlCaptureContext, @function
RtlCaptureContext:
    /* Save the complete integer bank before using a temporary. $a0 is both
     * the destination and a saved register, so it stays the base. */
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

    /* $ra is where this call returns, i.e. the PC this frame resumes at. */
    st.d    $ra,   $a0, CxPc
    st.d    $zero, $a0, CxPc + 8

    /* EUEN.FPE gates the FPU; reading it here would be legal even when the
     * user context has the FPU off. User mode runs with FPE enabled once it
     * uses the FPU, so the bank is saved only when it is on. */
    csrrd   $t0, LA64_CSR_EUEN
    andi    $t0, $t0, LA64_EUEN_FPE
    beqz    $t0, .Luser_no_fp

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

    movfcsr2gr $t0, $fcsr0
    st.w    $t0, $a0, CxFcsr
    li.d    $t0, LA64_CONTEXT_ALL
    st.w    $t0, $a0, CxContextFlags
    jr      $ra

.Luser_no_fp:
    st.w    $zero, $a0, CxFcsr
    li.d    $t0, (LA64_CONTEXT_CONTROL | LA64_CONTEXT_INTEGER)
    st.w    $t0, $a0, CxContextFlags
    jr      $ra
    .size   RtlCaptureContext, . - RtlCaptureContext

    .balign 4
    .globl DbgBreakPoint
    .globl DbgUserBreakPoint
    .type   DbgBreakPoint, @function
DbgBreakPoint:
DbgUserBreakPoint:
    break   0
    jr      $ra
    .size   DbgBreakPoint, . - DbgBreakPoint

    .balign 4
    .globl DbgBreakPointWithStatus
    .type   DbgBreakPointWithStatus, @function
DbgBreakPointWithStatus:
    break   0
    jr      $ra
    .size   DbgBreakPointWithStatus, . - DbgBreakPointWithStatus

    /* Private execution-environment service: $a0 selects the KD operation and
     * $a1-$a4 carry its arguments, exactly as the kernel dispatcher reads
     * them out of the trap frame. The id travels in $a7 like every other
     * service ([asm/syscalls.inc]). */
    .balign 4
    .globl DebugService
    .type   DebugService, @function
DebugService:
    li.d    $a7, LA64_DEBUG_SERVICE_ID
    syscall 0
    jr      $ra
    .size   DebugService, . - DebugService

    /* DebugService2(Argument1, Argument2, Service) is called with the service
     * in the third slot; rotate it into the dispatcher's register order. */
    .balign 4
    .globl DebugService2
    .type   DebugService2, @function
DebugService2:
    move    $t1, $a2
    move    $a2, $a1
    move    $a1, $a0
    move    $a0, $t1
    li.d    $a7, LA64_DEBUG_SERVICE_ID
    syscall 0
    jr      $ra
    .size   DebugService2, . - DebugService2

/* EOF */
