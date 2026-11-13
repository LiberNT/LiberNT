/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Compile-time proof that asmoffsets.h still matches the
 *              structures the LoongArch64 assembly walks
 *
 * Every constant in asmoffsets.h is asserted here against the real header.
 * The port cannot use the generated <asm/ksloongarch64.h> yet (that file is
 * empty on this target; see the explanation at the top of asmoffsets.h), so
 * this object is what turns a structure change into a build failure instead
 * of a silent trap-frame corruption.
 */

#include <ntoskrnl.h>
#include "asmoffsets.h"

/* ---------------------------------------------------------------------------
 * CONTEXT / KTRAP_FRAME
 * ------------------------------------------------------------------------ */
C_ASSERT(FIELD_OFFSET(CONTEXT, ContextFlags) == CxContextFlags);
C_ASSERT(FIELD_OFFSET(CONTEXT, Fcsr) == CxFcsr);
C_ASSERT(FIELD_OFFSET(CONTEXT, Fcc) == CxFcc);
C_ASSERT(FIELD_OFFSET(CONTEXT, Pc) == CxPc);
C_ASSERT(FIELD_OFFSET(CONTEXT, R) == CxR);
C_ASSERT(FIELD_OFFSET(CONTEXT, F) == CxF);
/* The slots trap.S saves and restores by name. $r0 has its own slot at CxR,
 * so $ra cannot be the first entry of the union. */
C_ASSERT(FIELD_OFFSET(CONTEXT, Ra) == Cx_Ra);
C_ASSERT(FIELD_OFFSET(CONTEXT, Tp) == Cx_Tp);
C_ASSERT(FIELD_OFFSET(CONTEXT, Sp) == Cx_Sp);
C_ASSERT(FIELD_OFFSET(CONTEXT, A0) == CxA0);
C_ASSERT(FIELD_OFFSET(CONTEXT, Fp) == CxFp);
C_ASSERT(FIELD_OFFSET(CONTEXT, S0) == CxS0);
C_ASSERT((CxT3 - CxT0) == 3 * sizeof(ULONG64));
C_ASSERT((CxS8 - CxS0) == 8 * sizeof(ULONG64));
C_ASSERT((CxF - CxR) == 32 * sizeof(ULONG64));

C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Badv) == TfBadv);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Crmd) == TfCrmd);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Prmd) == TfPrmd);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Euen) == TfEuen);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Ecfg) == TfEcfg);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Estat) == TfEstat);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Spare) == TfSpare);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, PreviousTrapFrame) == TfPreviousTrapFrame);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, PreviousIrql) == TfPreviousIrql);
C_ASSERT(sizeof(KTRAP_FRAME) == TrapFrameLength);

/* A frame is carved below the interrupted stack pointer, so its size and
 * every stack switch keep the 16-byte psABI alignment [ABI]. */
C_ASSERT((TrapFrameLength & 15) == 0);
C_ASSERT((SwitchFrameLength & 15) == 0);
C_ASSERT((CalloutFrameLength & 15) == 0);
C_ASSERT((StartFrameLength & 15) == 0);

/* ---------------------------------------------------------------------------
 * KSTART_FRAME / KSWITCH_FRAME / KCALLOUT_FRAME
 * ------------------------------------------------------------------------ */
C_ASSERT(FIELD_OFFSET(KSTART_FRAME, SystemRoutine) == SfSystemRoutine);
C_ASSERT(FIELD_OFFSET(KSTART_FRAME, StartRoutine) == SfStartRoutine);
C_ASSERT(FIELD_OFFSET(KSTART_FRAME, StartContext) == SfStartContext);
C_ASSERT(sizeof(KSTART_FRAME) == StartFrameLength);

C_ASSERT(FIELD_OFFSET(KSWITCH_FRAME, S1) == SwS1);
C_ASSERT(FIELD_OFFSET(KSWITCH_FRAME, S8) == SwS8);
C_ASSERT(FIELD_OFFSET(KSWITCH_FRAME, Fp) == SwFp);
C_ASSERT(FIELD_OFFSET(KSWITCH_FRAME, Sp) == SwSp);
C_ASSERT(FIELD_OFFSET(KSWITCH_FRAME, Ra) == SwRa);
C_ASSERT(FIELD_OFFSET(KSWITCH_FRAME, ApcBypass) == SwApcBypass);
C_ASSERT(sizeof(KSWITCH_FRAME) == SwitchFrameLength);

C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, S0) == CoS0);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, Fp) == CoFp);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, Ra) == CoRa);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, Fs0) == CoFs0);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, Fcsr) == CoFcsr);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, CallbackStack) == CoCallbackStack);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, InitialStack) == CoInitialStack);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, TrapFrame) == CoTrapFrame);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, OutputBuffer) == CoOutputBuffer);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, OutputLength) == CoOutputLength);
C_ASSERT(sizeof(KCALLOUT_FRAME) == CalloutFrameLength);

/* ---------------------------------------------------------------------------
 * KPCR / KPRCB / KTHREAD
 * ------------------------------------------------------------------------ */
C_ASSERT(FIELD_OFFSET(KPCR, CpuId) == PcCpuId);
C_ASSERT(FIELD_OFFSET(KPCR, CurrentIrql) == PcCurrentIrql);
C_ASSERT(FIELD_OFFSET(KPCR, PanicStack) == PcPanicStack);
C_ASSERT(FIELD_OFFSET(KPCR, PanicFrame) == PcPanicFrame);
C_ASSERT(FIELD_OFFSET(KPCR, Prcb) == PcPrcb);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentThread) == PbCurrentThread);
C_ASSERT((PcPrcb + PbCurrentThread) == PcPrcbCurrentThread);

/* The entry code reaches these two with the signed 12-bit displacement of
 * ld.bu/st.b (KiLa64GetCurrentIrql/KiLa64SetCurrentIrql in ke.h use the same
 * form), so both must stay below 2048. */
C_ASSERT(FIELD_OFFSET(KPCR, CurrentIrql) < 2048);
C_ASSERT(FIELD_OFFSET(KPCR, Prcb.CurrentThread) < 2048);

C_ASSERT(FIELD_OFFSET(KTHREAD, InitialStack) == ThInitialStack);
C_ASSERT(FIELD_OFFSET(KTHREAD, StackLimit) == ThStackLimit);
C_ASSERT(FIELD_OFFSET(KTHREAD, StackBase) == ThStackBase);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelStack) == ThKernelStack);
C_ASSERT(FIELD_OFFSET(KTHREAD, Running) == ThRunning);
C_ASSERT(FIELD_OFFSET(KTHREAD, TrapFrame) == ThTrapFrame);
C_ASSERT(FIELD_OFFSET(KTHREAD, Teb) == ThTeb);
C_ASSERT(FIELD_OFFSET(KTHREAD, CallbackStack) == ThCallbackStack);

/* ---------------------------------------------------------------------------
 * CSR numbers: the assembly copies must equal the C ones in
 * internal/loongarch64/ke.h.
 * ------------------------------------------------------------------------ */
C_ASSERT(LA64_CSR_CRMD == 0x00);
C_ASSERT(LA64_CSR_PRMD == 0x01);
C_ASSERT(LA64_CSR_EUEN == 0x02);
C_ASSERT(LA64_CSR_ECFG == 0x04);
C_ASSERT(LA64_CSR_ESTAT == 0x05);
C_ASSERT(LA64_CSR_ERA == 0x06);
C_ASSERT(LA64_CSR_BADV == 0x07);
C_ASSERT(LA64_CSR_EENTRY == 0x0c);
C_ASSERT(LA64_CSR_TICLR == 0x44);
C_ASSERT(LA64_CSR_TLBRENTRY == 0x88);
C_ASSERT(LA64_CSR_TLBRBADV == 0x89);
C_ASSERT(LA64_CSR_TLBRERA == 0x8a);
C_ASSERT(LA64_CSR_TLBRPRMD == 0x8f);

/* [V1-7.4.16]: SAVE0-SAVE15 are one contiguous 0x30..0x3f block. */
C_ASSERT((LA64_CSR_SAVE1 - LA64_CSR_SAVE0) == 1);
C_ASSERT((LA64_CSR_SAVE4 - LA64_CSR_SAVE0) == 4);

/* CONTEXT_FLOATING_POINT must be the only flag boundary the exit path tests
 * with a single bit. */
C_ASSERT(APC_LEVEL == LA64_IRQL_APC);
C_ASSERT(DISPATCH_LEVEL == LA64_IRQL_DISPATCH);

C_ASSERT((CONTEXT_FLOATING_POINT & LA64_CONTEXT_FP_FLAG) != 0);
C_ASSERT(((CONTEXT_CONTROL | CONTEXT_INTEGER) & LA64_CONTEXT_FP_FLAG) == 0);

/* EOF */
