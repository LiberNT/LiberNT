/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 exception entry and ERTN exit
 *
 * Two entry points share one prologue:
 *
 *   KiLa64TrapEntry      general exceptions and interrupts. Installed in
 *                        CSR.EENTRY, whose base is a virtual address with
 *                        bits 11:0 read-only zero, [V1-7.4.10]; with
 *                        ECFG.VS=0 every general exception and every line
 *                        interrupt enters it at offset 0, [V1-6.3.1].
 *   KiLa64TlbRefillEntry the TLB refill exception (TLBRD). Installed in
 *                        CSR.TLBRENTRY, which takes a PHYSICAL address and
 *                        runs with CRMD.DA=1, CRMD.PG=0, [V1-7.5.11]
 *                        [V1-6.3.4]. Because the kernel image, its stacks
 *                        and the page tables all live in the DMW1 direct map
 *                        (0x9000_0000_0000_0000 + PA), and a direct-mode
 *                        address is used as the low PALEN bits of itself,
 *                        [V1-2.1.7], the same instructions and the same C
 *                        handler are correct with translation off: only the
 *                        physical form of TLBRENTRY differs. That is why this
 *                        file does not need a second, physical-only walker.
 *
 * The interrupted stack is not a usable frame address for a user trap (the
 * user's $sp is data), so the prologue switches to the current thread's
 * resident kernel stack top, KTHREAD.InitialStack, exactly as the ticket's
 * blueprint does. A general exception or interrupt taken in kernel mode
 * continues on the interrupted stack, which keeps the trap frame in the
 * middle of that thread's own stack where KeSwitchKernelStack and the
 * exception machinery expect it.
 *
 * The frame is a KTRAP_FRAME: the whole GPR bank, the five mode CSRs the
 * manual defines for an exception ([V1-7.4.1]-[V1-7.4.6]: CRMD, PRMD, EUEN,
 * ECFG, ESTAT), BADV, and ERA as CONTEXT.Pc. CRMD.IE is 0 for the whole
 * handler: the hardware clears it on entry and the exit path returns through
 * ERTN, which restores CRMD from PRMD (or TLBRPRMD for TLBRD), [V1-7.4.1].
 *
 * Floating point: EUEN.FPE gates the FPU, [V1-7.4.3]. The prologue saves
 * f0-f31 and fcsr0 only when the faulting context was user mode AND FPE is
 * set; a kernel trap never touches the FPRs, so kernel code that never uses
 * the FPU owes it no save. Executing fst.d with FPE clear would itself take
 * an FPD exception inside the prologue, hence the check.
 */

#include "asmoffsets.h"

/*
 * Save the GPR bank that is still live, then the four scratch registers from
 * CSR.SAVE0-SAVE3. $t4 is the transfer register, so its own value is stored
 * first. Leaves $t2 (the frame base) advanced over the register block only.
 */
.macro LA64_SAVE_GPRS base
    st.d    $ra,  \base, Cx_Ra
    st.d    $tp,  \base, Cx_Tp
    st.d    $sp,  \base, Cx_Sp
    st.d    $a0,  \base, CxA0
    st.d    $a1,  \base, CxA1
    st.d    $a2,  \base, CxA2
    st.d    $a3,  \base, CxA3
    st.d    $a4,  \base, CxA4
    st.d    $a5,  \base, CxA5
    st.d    $a6,  \base, CxA6
    st.d    $a7,  \base, CxA7
    st.d    $t4,  \base, CxT4
    csrrd   $t4, LA64_CSR_SAVE0
    st.d    $t4,  \base, CxT0
    csrrd   $t4, LA64_CSR_SAVE1
    st.d    $t4,  \base, CxT1
    csrrd   $t4, LA64_CSR_SAVE2
    st.d    $t4,  \base, CxT2
    csrrd   $t4, LA64_CSR_SAVE3
    st.d    $t4,  \base, CxT3
    st.d    $t5,  \base, CxT5
    st.d    $t6,  \base, CxT6
    st.d    $t7,  \base, CxT7
    st.d    $t8,  \base, CxT8
    st.d    $r21, \base, CxR21
    st.d    $fp,  \base, CxFp
    st.d    $s0,  \base, CxS0
    st.d    $s1,  \base, CxS1
    st.d    $s2,  \base, CxS2
    st.d    $s3,  \base, CxS3
    st.d    $s4,  \base, CxS4
    st.d    $s5,  \base, CxS5
    st.d    $s6,  \base, CxS6
    st.d    $s7,  \base, CxS7
    st.d    $s8,  \base, CxS8
.endm

/*
 * Restore the GPR bank and return through ERTN. $t4 is read last because the
 * FP block above it uses $t4 as scratch; $sp is read after every other load
 * and no memory is touched after it.
 */
.macro LA64_RESTORE_GPRS
    ld.d    $ra,  $sp, Cx_Ra
    ld.d    $tp,  $sp, Cx_Tp
    ld.d    $a0,  $sp, CxA0
    ld.d    $a1,  $sp, CxA1
    ld.d    $a2,  $sp, CxA2
    ld.d    $a3,  $sp, CxA3
    ld.d    $a4,  $sp, CxA4
    ld.d    $a5,  $sp, CxA5
    ld.d    $a6,  $sp, CxA6
    ld.d    $a7,  $sp, CxA7
    ld.d    $t5,  $sp, CxT5
    ld.d    $t6,  $sp, CxT6
    ld.d    $t7,  $sp, CxT7
    ld.d    $t8,  $sp, CxT8
    ld.d    $fp,  $sp, CxFp
    ld.d    $s0,  $sp, CxS0
    ld.d    $s1,  $sp, CxS1
    ld.d    $s2,  $sp, CxS2
    ld.d    $s3,  $sp, CxS3
    ld.d    $s4,  $sp, CxS4
    ld.d    $s5,  $sp, CxS5
    ld.d    $s6,  $sp, CxS6
    ld.d    $s7,  $sp, CxS7
    ld.d    $s8,  $sp, CxS8
    ld.d    $t0,  $sp, CxT0
    ld.d    $t1,  $sp, CxT1
    ld.d    $t2,  $sp, CxT2
    ld.d    $t3,  $sp, CxT3
    ld.d    $t4,  $sp, CxT4
    ld.d    $sp,  $sp, Cx_Sp
    ertn
.endm

    .section .text$KiLa64TrapEntry, "ax", @progbits
    .balign 4096
    .global KiLa64TrapEntry
    .type   KiLa64TrapEntry, @function
/* void KiLa64TrapEntry(void) - CSR.EENTRY, ECFG.VS=0. */
KiLa64TrapEntry:
    /* Only four scratch GPRs are needed before the frame exists. The CSRs
     * hold them because $r21 (the KPCR base, [ABI] reserved) must survive
     * untouched and every other GPR still carries interrupted state. */
    csrwr   $t0, LA64_CSR_SAVE0
    csrwr   $t1, LA64_CSR_SAVE1
    csrwr   $t2, LA64_CSR_SAVE2
    csrwr   $t3, LA64_CSR_SAVE3
    li.d    $t0, 0
    csrwr   $t0, LA64_CSR_SAVE4
    b       KiLa64TrapCommon
    .size   KiLa64TrapEntry, . - KiLa64TrapEntry

    .section .text$KiLa64TlbRefillEntry, "ax", @progbits
    .balign 4096
    .global KiLa64TlbRefillEntry
    .type   KiLa64TlbRefillEntry, @function
/* void KiLa64TlbRefillEntry(void) - CSR.TLBRENTRY, a physical address. */
KiLa64TlbRefillEntry:
    csrwr   $t0, LA64_CSR_SAVE0
    csrwr   $t1, LA64_CSR_SAVE1
    csrwr   $t2, LA64_CSR_SAVE2
    csrwr   $t3, LA64_CSR_SAVE3
    li.d    $t0, 1
    csrwr   $t0, LA64_CSR_SAVE4
    /* fall through: KiLa64TrapCommon follows in the same section order */

    .section .text$KiLa64TrapCommon, "ax", @progbits
    .balign 4
    .type   KiLa64TrapCommon, @function
KiLa64TrapCommon:
    /* $t0 = vector marker, $t1 = the pre-exception mode information.
     * PRMD carries PPLV/PIE for a general exception, TLBRPRMD does the same
     * for TLBRD with the same bit positions, [V1-7.4.2] [V1-7.5.17]. */
    csrrd   $t0, LA64_CSR_SAVE4
    bnez    $t0, .Lrefill_prmd
    csrrd   $t1, LA64_CSR_PRMD
    b       .Lhave_prmd
.Lrefill_prmd:
    csrrd   $t1, LA64_CSR_TLBRPRMD
.Lhave_prmd:
    andi    $t2, $t1, LA64_PRMD_PPLV
    bnez    $t2, .Lfrom_user
    /* Kernel mode: the interrupted $sp is the thread's own kernel stack. */
    move    $t2, $sp
    b       .Lhave_stack
.Lfrom_user:
    /* User mode: $sp is user data and must never select the frame address. */
    ld.d    $t2, $r21, PcPrcbCurrentThread
    ld.d    $t2, $t2, ThInitialStack
.Lhave_stack:
    addi.d  $t2, $t2, -TrapFrameLength

    /* A frame that does not fit above the committed stack would fault inside
     * the prologue and recurse. Take the panic frame in the KPCR instead and
     * stop deterministically. */
    ld.d    $t3, $r21, PcPrcbCurrentThread
    ld.d    $t3, $t3, ThStackLimit
    sltu    $t3, $t2, $t3
    beqz    $t3, .Lframe_ok

    /* Panic path: the frame is the KPCR's own PanicFrame, the stack is the
     * reserved panic stack. Both are resident and independent of the failed
     * thread stack. */
    ld.d    $t2, $r21, PcPanicStack
    move    $sp, $t2
    addi.d  $t2, $r21, PcPanicFrame
    LA64_SAVE_GPRS $t2
    li.d    $t0, 2
    st.w    $t0, $t2, TfSpare
    st.w    $t1, $t2, TfPrmd
    csrrd   $t4, LA64_CSR_ERA
    st.d    $t4, $t2, TfPc
    csrrd   $t4, LA64_CSR_BADV
    st.d    $t4, $t2, TfBadv
    csrrd   $t4, LA64_CSR_CRMD
    st.w    $t4, $t2, TfCrmd
    csrrd   $t4, LA64_CSR_EUEN
    st.w    $t4, $t2, TfEuen
    csrrd   $t4, LA64_CSR_ECFG
    st.w    $t4, $t2, TfEcfg
    csrrd   $t4, LA64_CSR_ESTAT
    st.w    $t4, $t2, TfEstat
    ld.bu     $t4, $r21, PcCurrentIrql
    st.b    $t4, $t2, TfPreviousIrql
    st.d    $zero, $t2, TfPreviousTrapFrame
    li.d    $t4, 0x01000003
    st.w    $t4, $t2, TfContextFlags
    move    $a0, $t2
    b       KiLa64TrapStop

.Lframe_ok:
    LA64_SAVE_GPRS $t2

    /* Which vector, and the pre-exception mode information, are recorded in
     * the frame so the C side never has to re-read a CSR that a nested trap
     * may have overwritten. */
    st.w    $t0, $t2, TfSpare
    st.w    $t1, $t2, TfPrmd

    bnez    $t0, .Lrefill_csrs
    csrrd   $t4, LA64_CSR_ERA
    st.d    $t4, $t2, TfPc
    csrrd   $t4, LA64_CSR_BADV
    st.d    $t4, $t2, TfBadv
    b       .Lcsrs_done
.Lrefill_csrs:
    csrrd   $t4, LA64_CSR_TLBRERA
    st.d    $t4, $t2, TfPc
    csrrd   $t4, LA64_CSR_TLBRBADV
    st.d    $t4, $t2, TfBadv
.Lcsrs_done:
    csrrd   $t4, LA64_CSR_CRMD
    st.w    $t4, $t2, TfCrmd
    csrrd   $t4, LA64_CSR_EUEN
    st.w    $t4, $t2, TfEuen
    csrrd   $t4, LA64_CSR_ECFG
    st.w    $t4, $t2, TfEcfg
    csrrd   $t4, LA64_CSR_ESTAT
    st.w    $t4, $t2, TfEstat
    ld.bu     $t4, $r21, PcCurrentIrql
    st.b    $t4, $t2, TfPreviousIrql
    st.d    $zero, $t2, TfPreviousTrapFrame

    /* CONTEXT flags: a user trap with the FPU enabled captured the whole
     * register set; a kernel trap captured control and integer state only. */
    li.d    $t4, 0x01000003
    andi    $t0, $t1, LA64_PRMD_PPLV
    beqz    $t0, .Lflags_done
    csrrd   $t0, LA64_CSR_EUEN
    andi    $t0, $t0, LA64_EUEN_FPE
    beqz    $t0, .Lflags_done
    li.d    $t4, 0x01000007
.Lflags_done:
    st.w    $t4, $t2, TfContextFlags
    andi    $t0, $t4, LA64_CONTEXT_FP_FLAG
    beqz    $t0, .Lno_fp_save
    fst.d   $f0,  $t2, CxF + 8 * 0
    fst.d   $f1,  $t2, CxF + 8 * 1
    fst.d   $f2,  $t2, CxF + 8 * 2
    fst.d   $f3,  $t2, CxF + 8 * 3
    fst.d   $f4,  $t2, CxF + 8 * 4
    fst.d   $f5,  $t2, CxF + 8 * 5
    fst.d   $f6,  $t2, CxF + 8 * 6
    fst.d   $f7,  $t2, CxF + 8 * 7
    fst.d   $f8,  $t2, CxF + 8 * 8
    fst.d   $f9,  $t2, CxF + 8 * 9
    fst.d   $f10, $t2, CxF + 8 * 10
    fst.d   $f11, $t2, CxF + 8 * 11
    fst.d   $f12, $t2, CxF + 8 * 12
    fst.d   $f13, $t2, CxF + 8 * 13
    fst.d   $f14, $t2, CxF + 8 * 14
    fst.d   $f15, $t2, CxF + 8 * 15
    fst.d   $f16, $t2, CxF + 8 * 16
    fst.d   $f17, $t2, CxF + 8 * 17
    fst.d   $f18, $t2, CxF + 8 * 18
    fst.d   $f19, $t2, CxF + 8 * 19
    fst.d   $f20, $t2, CxF + 8 * 20
    fst.d   $f21, $t2, CxF + 8 * 21
    fst.d   $f22, $t2, CxF + 8 * 22
    fst.d   $f23, $t2, CxF + 8 * 23
    fst.d   $f24, $t2, CxF + 8 * 24
    fst.d   $f25, $t2, CxF + 8 * 25
    fst.d   $f26, $t2, CxF + 8 * 26
    fst.d   $f27, $t2, CxF + 8 * 27
    fst.d   $f28, $t2, CxF + 8 * 28
    fst.d   $f29, $t2, CxF + 8 * 29
    fst.d   $f30, $t2, CxF + 8 * 30
    fst.d   $f31, $t2, CxF + 8 * 31
    /* fcsr0 is reached with movfcsr2gr, there is no CSR number for it,
     * [V1-3.2.4.1]; Fcsr is a 32-bit field of CONTEXT. */
    movfcsr2gr $t0, $fcsr0
    st.w    $t0, $t2, CxFcsr
    b       .Lfp_done
.Lno_fp_save:
    /* Publish an explicit zero rather than whatever the stack held, so a
     * CONTEXT copied out of this frame carries a defined Fcsr. */
    st.w    $zero, $t2, CxFcsr
.Lfp_done:

    /* Kernel code reads the current TEB through $tp (NtCurrentTeb). After a
     * user trap that is the thread's TEB, never the value user mode left. */
    andi    $t0, $t1, LA64_PRMD_PPLV
    beqz    $t0, .Lkernel_tp
    ld.d    $t0, $r21, PcPrcbCurrentThread
    ld.d    $tp, $t0, ThTeb
.Lkernel_tp:

    /* The frame is complete and the stack is now consistent. */
    move    $sp, $t2
    /* End this trap's C frame chain; the interrupted $fp is in the frame. */
    move    $fp, $zero

    move    $a0, $sp
    csrrd   $t0, LA64_CSR_SAVE4
    csrwr   $zero, LA64_CSR_SAVE4
    bnez    $t0, .Lcall_refill
    bl      KiLa64TrapHandler
    b       .Lexit
.Lcall_refill:
    /* The TLB refill path must run with interrupts off and must not block:
     * it either fills the TLB pair from the page tables or installs an
     * invalid pair so the retried access reports PIL/PIS/PIF. */
    bl      KiLa64TlbRefillHandler
.Lexit:

    /* CONTEXT_FLOATING_POINT says whether the FPR bank belongs to this
     * frame. Restoring it when the frame never captured it would publish
     * another thread's registers. */
    ld.w      $t4, $sp, TfContextFlags
    andi    $t4, $t4, LA64_CONTEXT_FP_FLAG
    beqz    $t4, .Lno_fp_restore
    ld.w      $t4, $sp, CxFcsr
    movgr2fcsr $fcsr0, $t4
    fld.d   $f0,  $sp, CxF + 8 * 0
    fld.d   $f1,  $sp, CxF + 8 * 1
    fld.d   $f2,  $sp, CxF + 8 * 2
    fld.d   $f3,  $sp, CxF + 8 * 3
    fld.d   $f4,  $sp, CxF + 8 * 4
    fld.d   $f5,  $sp, CxF + 8 * 5
    fld.d   $f6,  $sp, CxF + 8 * 6
    fld.d   $f7,  $sp, CxF + 8 * 7
    fld.d   $f8,  $sp, CxF + 8 * 8
    fld.d   $f9,  $sp, CxF + 8 * 9
    fld.d   $f10, $sp, CxF + 8 * 10
    fld.d   $f11, $sp, CxF + 8 * 11
    fld.d   $f12, $sp, CxF + 8 * 12
    fld.d   $f13, $sp, CxF + 8 * 13
    fld.d   $f14, $sp, CxF + 8 * 14
    fld.d   $f15, $sp, CxF + 8 * 15
    fld.d   $f16, $sp, CxF + 8 * 16
    fld.d   $f17, $sp, CxF + 8 * 17
    fld.d   $f18, $sp, CxF + 8 * 18
    fld.d   $f19, $sp, CxF + 8 * 19
    fld.d   $f20, $sp, CxF + 8 * 20
    fld.d   $f21, $sp, CxF + 8 * 21
    fld.d   $f22, $sp, CxF + 8 * 22
    fld.d   $f23, $sp, CxF + 8 * 23
    fld.d   $f24, $sp, CxF + 8 * 24
    fld.d   $f25, $sp, CxF + 8 * 25
    fld.d   $f26, $sp, CxF + 8 * 26
    fld.d   $f27, $sp, CxF + 8 * 27
    fld.d   $f28, $sp, CxF + 8 * 28
    fld.d   $f29, $sp, CxF + 8 * 29
    fld.d   $f30, $sp, CxF + 8 * 30
    fld.d   $f31, $sp, CxF + 8 * 31
.Lno_fp_restore:
    LA64_RESTORE_GPRS

    .section .text$KiLa64RestoreTrapFrame, "ax", @progbits
    .balign 4
    .global KiLa64RestoreTrapFrame
    .type   KiLa64RestoreTrapFrame, @function
/* void KiLa64RestoreTrapFrame(PKTRAP_FRAME Frame)
 *
 * Resume a prepared frame from C: the caller states that the frame is the
 * state to return to (NtContinue, a first user thread, a callback return).
 * ERTN restores CRMD.PLV and CRMD.IE from PRMD.PIE, so user mode can only be
 * re-entered when the frame says it was a user frame. */
KiLa64RestoreTrapFrame:
    move    $sp, $a0
    b       .Lno_fp_restore
    .size   KiLa64RestoreTrapFrame, . - KiLa64RestoreTrapFrame
