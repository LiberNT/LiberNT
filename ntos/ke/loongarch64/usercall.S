/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 kernel call to user mode and callback return
 *
 * KiLa64CallUserMode suspends the kernel side of a user-mode callback in a
 * KCALLOUT_FRAME on the current kernel stack and enters user mode through a
 * prepared trap frame (KiLa64ReturnToUser ends in ERTN). KCALLOUT_FRAME
 * holds exactly the psABI callee-saved integer bank (s0-s8, fp), the return
 * address, the callee-saved FP bank (f24-f31 = fs0-fs7) and fcsr0, so the
 * kernel context is fully reconstructed when NtCallbackReturn resumes it.
 *
 * The FP bank is saved only when EUEN.FPE is set: with the FPU disabled a
 * single fst.d would take an FPD exception (Ecode 0xf, [V1-7.4.6]) inside
 * the callout.
 */

#include "asmoffsets.h"

    .text
    .balign 4
    .global KiLa64CallUserMode
    .type   KiLa64CallUserMode, @function
/* NTSTATUS KiLa64CallUserMode(PKTRAP_FRAME Frame, PVOID *OutputBuffer,
 *                             PULONG OutputLength) */
KiLa64CallUserMode:
    addi.d  $sp, $sp, -CalloutFrameLength
    st.d    $s0, $sp, CoS0
    st.d    $s1, $sp, CoS1
    st.d    $s2, $sp, CoS2
    st.d    $s3, $sp, CoS3
    st.d    $s4, $sp, CoS4
    st.d    $s5, $sp, CoS5
    st.d    $s6, $sp, CoS6
    st.d    $s7, $sp, CoS7
    st.d    $s8, $sp, CoS8
    st.d    $fp, $sp, CoFp
    st.d    $ra, $sp, CoRa
    csrrd   $t0, LA64_CSR_CRMD
    st.w    $t0, $sp, CoCrmd
    csrrd   $t0, LA64_CSR_EUEN
    andi    $t0, $t0, LA64_EUEN_FPE
    beqz    $t0, .Lcallout_no_fp
    fst.d   $f24, $sp, CoFs0
    fst.d   $f25, $sp, CoFs1
    fst.d   $f26, $sp, CoFs2
    fst.d   $f27, $sp, CoFs3
    fst.d   $f28, $sp, CoFs4
    fst.d   $f29, $sp, CoFs5
    fst.d   $f30, $sp, CoFs6
    fst.d   $f31, $sp, CoFs7
    movfcsr2gr $t0, $fcsr0
    st.w    $t0, $sp, CoFcsr
    b       .Lcallout_fp_done
.Lcallout_no_fp:
    st.w    $zero, $sp, CoFcsr
.Lcallout_fp_done:
    st.d    $a1, $sp, CoOutputBuffer
    st.d    $a2, $sp, CoOutputLength
    /* a0 is already the prepared trap frame, a1 the callout frame. */
    move    $a1, $sp
    bl      KiLa64UserModeCallout
    /* Only a preparation failure returns here; success leaves through ERTN. */
    b       .Lcallback_resume
    .size   KiLa64CallUserMode, . - KiLa64CallUserMode

    .balign 4
    .global KiLa64CallbackReturn
    .type   KiLa64CallbackReturn, @function
/* VOID KiLa64CallbackReturn(PKCALLOUT_FRAME CalloutFrame, NTSTATUS Status)
 *
 * NtCallbackReturn has already restored the kernel-owned thread links. Keep
 * interrupts masked until the original $sp and register bank agree again. */
KiLa64CallbackReturn:
    move    $sp, $a0
    move    $a0, $a1
.Lcallback_resume:
    li.d    $t0, 0
    li.d    $t1, 0x4
    csrxchg $t0, $t1, LA64_CSR_CRMD
    csrrd   $t0, LA64_CSR_EUEN
    andi    $t0, $t0, LA64_EUEN_FPE
    beqz    $t0, .Lcallback_no_fp
    ld.w      $t0, $sp, CoFcsr
    movgr2fcsr $fcsr0, $t0
    fld.d   $f24, $sp, CoFs0
    fld.d   $f25, $sp, CoFs1
    fld.d   $f26, $sp, CoFs2
    fld.d   $f27, $sp, CoFs3
    fld.d   $f28, $sp, CoFs4
    fld.d   $f29, $sp, CoFs5
    fld.d   $f30, $sp, CoFs6
    fld.d   $f31, $sp, CoFs7
.Lcallback_no_fp:
    /* CRMD.IE is restored from the saved image, but only that bit: PLV, DA,
     * PG, DATF and DATM are kernel state that must not be replayed from a
     * stack frame ([V1-7.4.1]). */
    ld.w      $t0, $sp, CoCrmd
    andi    $t0, $t0, 0x4
    ld.d    $s0, $sp, CoS0
    ld.d    $s1, $sp, CoS1
    ld.d    $s2, $sp, CoS2
    ld.d    $s3, $sp, CoS3
    ld.d    $s4, $sp, CoS4
    ld.d    $s5, $sp, CoS5
    ld.d    $s6, $sp, CoS6
    ld.d    $s7, $sp, CoS7
    ld.d    $s8, $sp, CoS8
    ld.d    $fp, $sp, CoFp
    ld.d    $ra, $sp, CoRa
    addi.d  $sp, $sp, CalloutFrameLength
    beqz    $t0, .Lcallback_masked
    li.d    $t1, 0x4
    csrxchg $t1, $t1, LA64_CSR_CRMD
.Lcallback_masked:
    jr      $ra
    .size   KiLa64CallbackReturn, . - KiLa64CallbackReturn
