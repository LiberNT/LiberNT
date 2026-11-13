/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Structure offsets and CSR numbers the LoongArch64 trap,
 *              switch and callout assembly uses
 *
 * Why this file exists instead of <asm/ksloongarch64.h>
 * -----------------------------------------------------
 * Every other port takes these numbers from the generated
 * <asm/ks<arch>.h>, built by genincdata.c (sdk/include/asm) and extracted by
 * sdk/tools/geninc/geninc.c from the .asmdef section of the target PE image.
 * On this target that pipeline produces an EMPTY header:
 *
 *   $ wc -l output-Clang-loongarch64-debug/sdk/include/asm/ksloongarch64.h
 *   0 ...
 *
 * The table itself is intact in the intermediate ELF
 * (genincdata.dll.elf, section .asmdef, 0xb340 bytes, first record
 * {TYPE_HEADER, "Context"}), but the PE that geninc.c is pointed at has that
 * section filled with zeroes (raw size 0xb800 at file offset 0x1000, all
 * bytes zero), so the tool reads the TYPE_END record immediately. That is a
 * build-tool defect outside this directory (see INFO_NEEDED in the port
 * report); until it is fixed, the assembly below cannot use the generated
 * header.
 *
 * The numbers are therefore carried here, and `asmframes.c` in this same
 * directory proves every one of them with C_ASSERT against the real
 * structures. A structure change that moves a field breaks that object, not
 * the running kernel.
 */

#pragma once

/* ---------------------------------------------------------------------------
 * CONTEXT (sdk/include/ndk/loongarch64/ketypes.h)
 *
 * CxR is the base of the 32-entry GPR union and CxF the base of the 32-entry
 * FPR array; both are consecutive 8-byte slots, so assembly addresses the
 * individual registers with CxR + 8*n / CxF + 8*n and only the two bases are
 * named here. Verified by asmframes.c.
 * ------------------------------------------------------------------------ */
#define CxContextFlags   0x000
#define CxFcsr           0x004
#define CxFcc            0x008
#define CxPc             0x010
#define CxR              0x020
#define CxF              0x120

/* Register slot indices inside CxR (psABI [ABI]: r1 ra, r2 tp, r3 sp,
 * r4-r11 a0-a7, r12-r20 t0-t8, r21 reserved, r22 fp, r23-r31 s0-s8). */
#define Cx_Ra            0x028
#define Cx_Tp            0x030
#define Cx_Sp            0x038
#define CxA0             0x040
#define CxA1             0x048
#define CxA2             0x050
#define CxA3             0x058
#define CxA4             0x060
#define CxA5             0x068
#define CxA6             0x070
#define CxA7             0x078
#define CxT0             0x080
#define CxT1             0x088
#define CxT2             0x090
#define CxT3             0x098
#define CxT4             0x0A0
#define CxT5             0x0A8
#define CxT6             0x0B0
#define CxT7             0x0B8
#define CxT8             0x0C0
#define CxR21            0x0C8
#define CxFp             0x0D0
#define CxS0             0x0D8
#define CxS1             0x0E0
#define CxS2             0x0E8
#define CxS3             0x0F0
#define CxS4             0x0F8
#define CxS5             0x100
#define CxS6             0x108
#define CxS7             0x110
#define CxS8             0x118

/* KTRAP_FRAME tail: the five mode CSRs, BADV, the frame link and the IRQL
 * the trap interrupted. */
#define TfContext          0x000
#define TfContextFlags     (TfContext + CxContextFlags)
#define TfPc               (TfContext + CxPc)
#define TfBadv            0x220
#define TfCrmd            0x228
#define TfPrmd            0x22c
#define TfEuen            0x230
#define TfEcfg            0x234
#define TfEstat           0x238
#define TfSpare           0x23c
#define TfPreviousTrapFrame 0x240
#define TfPreviousIrql    0x248
#define TrapFrameLength   0x260

/* KSTART_FRAME: SystemRoutine, StartRoutine, StartContext, Return. */
#define SfSystemRoutine   0x000
#define SfStartRoutine    0x008
#define SfStartContext    0x010
#define StartFrameLength  0x020

/* KSWITCH_FRAME: S0-S8, Fp, Sp, Ra, ApcBypass (byte), 15 reserved bytes. */
#define SwS0             0x000
#define SwS1             0x008
#define SwS2             0x010
#define SwS3             0x018
#define SwS4             0x020
#define SwS5             0x028
#define SwS6             0x030
#define SwS7             0x038
#define SwS8             0x040
#define SwFp             0x048
#define SwSp             0x050
#define SwRa             0x058
#define SwApcBypass      0x060
#define SwitchFrameLength 0x070

/* KCALLOUT_FRAME: S0-S8, Fp, Sp, Ra, Fs0-Fs7, Crmd, Fcsr, CallbackStack,
 * InitialStack, TrapFrame, OutputBuffer, OutputLength. */
#define CoS0             0x000
#define CoS1             0x008
#define CoS2             0x010
#define CoS3             0x018
#define CoS4             0x020
#define CoS5             0x028
#define CoS6             0x030
#define CoS7             0x038
#define CoS8             0x040
#define CoFp             0x048
#define CoSp             0x050
#define CoRa             0x058
#define CoFs0            0x060
#define CoFs1            0x068
#define CoFs2            0x070
#define CoFs3            0x078
#define CoFs4            0x080
#define CoFs5            0x088
#define CoFs6            0x090
#define CoFs7            0x098
#define CoCrmd           0x0A0
#define CoFcsr           0x0A4
#define CoCallbackStack  0x0A8
#define CoInitialStack   0x0B0
#define CoTrapFrame      0x0B8
#define CoOutputBuffer   0x0C0
#define CoOutputLength   0x0C8
#define CalloutFrameLength 0x0D0

/* KPCR / KPRCB. */
#define PcCpuId           0x000
#define PcCurrentIrql     0x008
#define PcPanicStack      0x010
#define PcPanicFrame      0x020
#define PcPrcb            0x280
#define PcPrcbCurrentThread 0x280       /* PcPrcb + PbCurrentThread */

#define PbCurrentThread   0x000
#define PbNextThread      0x008
#define PbIdleThread      0x010
#define PbNumber          0x018
#define PbMHz             0x01c
#define PbProcessorState  0x020
#define PbDpcStack        0x2168

/* KTHREAD fields the entry and switch code needs. KTHREAD is shared, so
 * asmframes.c checks these too. */
#define ThInitialStack    0x028
#define ThStackLimit      0x030
#define ThStackBase       0x038
#define ThKernelStack     0x058
#define ThRunning         0x069
#define ThTrapFrame       0x088
#define ThTeb             0x0e8
#define ThCallbackStack   0x458

/* ---------------------------------------------------------------------------
 * CSR numbers, [V1-7.4.x], [V1-7.5.x], [V1-7.6.x].
 *
 * The same names and values exist in internal/loongarch64/ke.h for C; this
 * copy is guarded so a translation unit that includes both (asmframes.c)
 * sees one definition and assembly that cannot include ke.h still has them.
 * The spellings are the ones ke.h uses so the guards actually collide.
 * ------------------------------------------------------------------------ */
#ifndef LA64_CSR_CRMD
#define LA64_CSR_CRMD       0x00
#endif
#ifndef LA64_CSR_PRMD
#define LA64_CSR_PRMD       0x01
#endif
#ifndef LA64_CSR_EUEN
#define LA64_CSR_EUEN       0x02
#endif
#ifndef LA64_CSR_ECFG
#define LA64_CSR_ECFG       0x04
#endif
#ifndef LA64_CSR_ESTAT
#define LA64_CSR_ESTAT      0x05
#endif
#ifndef LA64_CSR_ERA
#define LA64_CSR_ERA        0x06
#endif
#ifndef LA64_CSR_BADV
#define LA64_CSR_BADV       0x07
#endif
#ifndef LA64_CSR_EENTRY
#define LA64_CSR_EENTRY     0x0C
#endif
#ifndef LA64_CSR_ASID
#define LA64_CSR_ASID       0x18
#endif
#ifndef LA64_CSR_PGDL
#define LA64_CSR_PGDL       0x19
#endif
#ifndef LA64_CSR_PGDH
#define LA64_CSR_PGDH       0x1A
#endif
/* [V1-7.4.16] CSR.SAVE0-CSR.SAVE15 are software data-save slots, 0x30+n.
 * The trap prologue is the only user: SAVE0-SAVE3 hold the four scratch
 * registers it must clobber before they can be stored to the frame, and
 * SAVE4 holds the vector marker (0 = general entry, 1 = TLB refill entry).
 * They are dead once the prologue has written the frame, so a trap that
 * arrives from C code (after the prologue, with interrupts deliberately
 * re-enabled) may reuse them. */
#ifndef LA64_CSR_SAVE0
#define LA64_CSR_SAVE0      0x30
#endif
#ifndef LA64_CSR_SAVE1
#define LA64_CSR_SAVE1      0x31
#endif
#ifndef LA64_CSR_SAVE2
#define LA64_CSR_SAVE2      0x32
#endif
#ifndef LA64_CSR_SAVE3
#define LA64_CSR_SAVE3      0x33
#endif
#ifndef LA64_CSR_SAVE4
#define LA64_CSR_SAVE4      0x34
#endif
#ifndef LA64_CSR_TICLR
#define LA64_CSR_TICLR      0x44
#endif
#ifndef LA64_CSR_TLBRENTRY
#define LA64_CSR_TLBRENTRY  0x88
#endif
#ifndef LA64_CSR_TLBRBADV
#define LA64_CSR_TLBRBADV   0x89
#endif
#ifndef LA64_CSR_TLBRERA
#define LA64_CSR_TLBRERA    0x8A
#endif
#ifndef LA64_CSR_TLBRPRMD
#define LA64_CSR_TLBRPRMD   0x8F
#endif

/* PRMD.PPLV lives in bits 1:0, [V1-7.4.2]. */
#define LA64_PRMD_PPLV       0x3

/* EUEN.FPE is bit 0, [V1-7.4.3]. The trap prologue saves the FPR bank only
 * when it is set: with FPE clear an `fst.d` would itself take an FPD
 * exception (Ecode 0xf) inside the prologue. */
#define LA64_EUEN_FPE        0x1

/* CONTEXT_FLOATING_POINT (0x01000000 | 4). Testing bit 2 is enough because
 * every CONTEXT_LOONGARCH64 set carries the same 0x01000000 base. */
#define LA64_CONTEXT_FP_FLAG 0x4

/* Interrupt request levels used as immediate operands by KiThreadStartup.
 * The NDK publishes APC_LEVEL as 1 and DISPATCH_LEVEL as 2 for this target
 * (sdk/include/xdk/loongarch64/ke.h); asmframes.c asserts the copies. */
#define LA64_IRQL_APC      1
#define LA64_IRQL_DISPATCH 2
