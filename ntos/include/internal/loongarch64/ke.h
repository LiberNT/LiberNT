/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     LoongArch64 kernel-private interfaces
 *
 * Sources for the architecture constants in this file (see
 * /mnt/data/la64-dev/docs/loongarch-abi-notes.md for the transcription and
 * the tag legend, and /mnt/data/la64-dev/docs/loongarch-handoff-contract.md
 * for the decisions this port already took):
 *
 *   [V1-7.4.1]   CRMD: PLV 1:0, IE 2, DA 3, PG 4, DATF 6:5, DATM 8:7, WE 9
 *   [V1-7.4.2]   PRMD: PPLV 1:0, PIE 2, PWE 3
 *   [V1-7.4.5]   ECFG: LIE 12:0, VS 18:16
 *   [V1-7.4.6]   ESTAT: IS 12:0, Ecode 21:16, EsubCode 30:22
 *   [V1-6.1.1]   ESTAT.IS line map: SWI0/SWI1, HWI0-HWI7, PMCOV, TI, IPI
 *   [V1-6.1.2]   interrupt priority: the higher int number wins
 *   [V1-4.2.1]   CSRRD/CSRWR/CSRXCHG (CSR address is a 23:10 immediate)
 *   [V1-4.2.4.7] INVTLB: Table 13 op codes 0x0-0x6 and the rj/rk operand rules
 *   [V1-7.2.2]   fixed 32-bit CSRs sign-extend into the 64-bit GPR on a read
 *   [V1-5.4.2]   TLB entry VPPN / even-odd page selection
 *   [L-TLB]      linux arch/loongarch/include/asm/tlb.h, Loongson copyright:
 *                Linux's names for the INVTLB ops (cross-check of the above;
 *                its "INVTLB_CURRENT_ALL" is not a manual operation)
 *   [L-INST]     linux arch/loongarch/include/asm/inst.h, Loongson copyright:
 *                INSN_BREAK = 0x002a0000
 *   [ABI]        LoongArch ELF psABI: $r21 is "Reserved (Non-allocatable)",
 *                so nothing the compiler emits ever touches it and the Linux
 *                kernel keeps its per-CPU base there
 *   [DOC-1.1.1]  docs.kernel.org "Introduction to LoongArch": $r21/$u0 =
 *                percpu base address
 *
 * The breakpoint and barrier primitives below are the only place that had to
 * pick an encoding: every other value is read straight from the tables above.
 */

#pragma once

/* No 32-bit guest ABI is implemented on this port (see the WOW64 limits in
 * internal/loongarch64/mm.h), so the emulated user machine follows the other
 * non-x86 ports and there is no secondary system DLL to report. */
#define KI_EMULATED_USER_MACHINE IMAGE_FILE_MACHINE_AMD64
#define KI_EMULATED_USER_SYSTEM_DLL NULL

/* ExSidQuerySystemInformation reports the emulated machine of the calling
 * process here. There is no LoongArch64 emulation of a second machine, so
 * the answer is the WOW64 one the other non-x86 ports give. */
#define KiEmulationProcessorArchitecture(Process) PROCESSOR_ARCHITECTURE_INTEL

/* The native architecture id KeProcessorArchitecture carries. No Windows SDK
 * assigns one to LoongArch64; this is a port-private value in the free slot
 * above PROCESSOR_ARCHITECTURE_RISCV64, like that port's own id. */
#ifndef PROCESSOR_ARCHITECTURE_LOONGARCH
#define PROCESSOR_ARCHITECTURE_LOONGARCH 16
#endif

/* Native assembly entry used by KD to recognize user callback transitions.
 * Like KiCallUserMode on the other ports this is entered from a user-mode
 * callback stub; the LoongArch64 ke port supplies the body. */
#define KI_USER_MODE_CALLBACK_ENTRY KiLa64CallUserMode
/* kernmain.c enters a user-mode callback through this name; the LoongArch64
 * body is the assembly routine in usercall.S. */
#define KiArchCallUserMode(Frame, Result, ResultLength) \
    KiLa64CallUserMode((Frame), (Result), (ResultLength))

NTSTATUS
NTAPI
KiLa64CallUserMode(
    _In_ PKTRAP_FRAME Frame,
    _Out_ PVOID *OutputBuffer,
    _Out_ PULONG OutputLength);

#define KI_CORE_PARKING

#define KiPrcbTimerTableLockQueue(Prcb, Index) (&(Prcb)->TimerTableLockQueue[(Index)])

/* The switch backend completes the outgoing-thread handoff on the new stack. */
#define KI_ARCH_THREAD_HANDOFF_USES_RUNNING 1

/* Software synchronization priority, below the clock and IPI levels. The
 * NDK type header already fixes this value for the port. */
#ifndef SYNCH_LEVEL
#define SYNCH_LEVEL 12
#endif

/* CSR numbers, [V1-7.4.x] and [V1-7.5.x]. The address is a 23:10 immediate
 * of csrrd/csrwr/csrxchg, so every user below spells it out literally. */
#define LA64_CSR_CRMD       0x00
#define LA64_CSR_PRMD       0x01
#define LA64_CSR_EUEN       0x02
#define LA64_CSR_ECFG       0x04
#define LA64_CSR_ESTAT      0x05
#define LA64_CSR_ERA        0x06
#define LA64_CSR_BADV       0x07
#define LA64_CSR_EENTRY     0x0C
#define LA64_CSR_TLBIDX     0x10
#define LA64_CSR_TLBEHI     0x11
#define LA64_CSR_TLBELO0    0x12
#define LA64_CSR_TLBELO1    0x13
#define LA64_CSR_ASID       0x18
#define LA64_CSR_PGDL       0x19
#define LA64_CSR_PGDH       0x1A
#define LA64_CSR_PGD        0x1B
#define LA64_CSR_PWCL       0x1C
#define LA64_CSR_PWCH       0x1D
#define LA64_CSR_STLBPS     0x1E
#define LA64_CSR_CPUID      0x20
#define LA64_CSR_SAVE0      0x30
#define LA64_CSR_TICLR      0x44
#define LA64_CSR_LLBCTL     0x60
#define LA64_CSR_TLBRENTRY  0x88
#define LA64_CSR_TLBRBADV   0x89
#define LA64_CSR_TLBRERA    0x8A
#define LA64_CSR_TLBRSAVE   0x8B
#define LA64_CSR_TLBRELO0   0x8C
#define LA64_CSR_TLBRELO1   0x8D
#define LA64_CSR_TLBREHI    0x8E
#define LA64_CSR_TLBRPRMD   0x8F
#define LA64_CSR_DMW0       0x180
#define LA64_CSR_DMW1       0x181
#define LA64_CSR_DMW2       0x182
#define LA64_CSR_DMW3       0x183

/* The CSR values themselves are 32 bits wide on LP64 (CRMD/PRMD/ECFG/ESTAT
 * fields all sit below bit 32), so the masks use 'u' rather than 'UL', which
 * would be 64-bit in this ABI and trips -Wshorten-64-to-32 at every use. */

/* CRMD fields, [V1-7.4.1]. PLV 0 is kernel, PLV 3 is user. */
#define LA64_CRMD_PLV_MASK  0x00000003u
#define LA64_CRMD_PLV_USER  0x00000003u
#define LA64_CRMD_PLV_KERNEL 0x00000000u
#define LA64_CRMD_IE        0x00000004u
#define LA64_CRMD_DA        0x00000008u
#define LA64_CRMD_PG        0x00000010u

/* PRMD fields, [V1-7.4.2]. Exception entry copies CRMD.PLV/IE into
 * PRMD.PPLV/PIE, so a trap frame answers "was I in user mode" and "were
 * interrupts on" from PRMD, never from the saved CRMD (which is already the
 * post-entry kernel state). */
#define LA64_PRMD_PPLV_MASK 0x00000003u
#define LA64_PRMD_PPLV_USER 0x00000003u
#define LA64_PRMD_PIE       0x00000004u

/* ECFG / ESTAT interrupt-line bits, [V1-7.4.5] [V1-7.4.6] [V1-6.1.1].
 * ECFG.LIE and ESTAT.IS use one bit position per line, so the mask for a
 * line is the same on both sides. The core records 13 lines and the int
 * number is the bit index, 0 through 12.
 *
 * Which IRQL a line is delivered at is HAL policy (ntos/ke dispatches on the
 * pending bits), not a property of the ISA. The one ISA constraint that
 * policy must respect is [V1-6.1.2]: a higher int number is a higher
 * priority, so the HAL has to hand out IRQLs monotonically in int number
 * (timer on IS[11] at CLOCK_LEVEL, IPI on IS[12] at IPI_LEVEL, HWI0..HWI7 at
 * the IRQLs between APC_LEVEL and SYNCH_LEVEL). NT's own ordering then holds
 * because every raise masks the lower lines through ECFG.LIE; the core never
 * arbitrates NT IRQLs for us, it only orders the lines it is allowed to take.
 *
 * TODO: ESTAT.IS is 12:0 in [V1-7.4.6]; message interrupts (CSR.MSGIS0-3,
 * [V1-6.2]) are reported in a separate CSR set, yet Loongson's own kernels
 * read a message-interrupt pending bit at IS[14]. This port defines the 13
 * line interrupts only — decide whether the kernel ever consumes message
 * interrupts before widening LA64_ESTAT_IS_MASK. */
#define LA64_IRQ_SWI0       0
#define LA64_IRQ_SWI1       1
#define LA64_IRQ_HWI0       2
#define LA64_IRQ_PMCOV      10
#define LA64_IRQ_TIMER      11
#define LA64_IRQ_IPI        12
#define LA64_IRQ_HWI(Line)  (2 + (Line))     /* HWI0..HWI7 live in IS[9:2] */
#define LA64_IRQ_BIT(Line)  (1u << (Line))
#define LA64_ECFG_LIE       LA64_IRQ_BIT
#define LA64_ESTAT_IS       LA64_IRQ_BIT
#define LA64_ECFG_LIE_MASK  0x00001FFFu      /* LIE[12:0] */
#define LA64_ESTAT_IS_MASK  0x00001FFFu      /* IS[12:0] */

/* ASID is 10 bits wide, [V1-7.5.4]; ASIDBITS[23:16] reports the
 * implementation's width and Linux derives the mask from it at run time. */
#define LA64_ASID_MASK      0x000003FFu

/* invtlb operation codes, [V1-4.2.4.7] Table 13. op is a 5-bit immediate,
 * rj holds the ASID in [9:0] with every other bit zero, rk holds the VA, and
 * the operand an op does not need must be $r0.
 *
 *   0x0  every entry, any ASID, G=1 or G=0
 *   0x1  "exactly the same as op=0" — the manual defines no difference
 *   0x2  every G=1 entry
 *   0x3  every G=0 entry
 *   0x4  G=0 and ASID == rj
 *   0x5  (G=0 and ASID == rj) and VA == rk
 *   0x6  (G=1 or ASID == rj) and VA == rk
 *
 * There is no "current address space only" op: the whole-space flush of one
 * ASID is 0x4 with CSR.ASID in rj. Linux's enum calls 0x1 INVTLB_CURRENT_ALL
 * ("invalid current tlb"), a name the manual does not support; 0x1 keeps the
 * effect of 0x0 here. */
#define LA64_INVTLB_ALL_ENTRIES         0x0
#define LA64_INVTLB_ALL_ENTRIES_ALIAS   0x1
#define LA64_INVTLB_ALL_GLOBAL          0x2
#define LA64_INVTLB_ALL_NONGLOBAL       0x3
#define LA64_INVTLB_ASID_NONGLOBAL      0x4
#define LA64_INVTLB_ADDR_NONGLOBAL      0x5
#define LA64_INVTLB_ADDR_GLOBAL_OR_ASID 0x6

/* ---------------------------------------------------------------------------
 * CSR accessors and barriers
 * ------------------------------------------------------------------------ */

FORCEINLINE
ULONG
KiLa64ReadCrmd(VOID)
{
    ULONG Value;

    __asm__ __volatile__("csrrd %0, 0x0" : "=r"(Value));
    return Value;
}

FORCEINLINE
VOID
KiLa64WriteCrmd(_In_ ULONG Value)
{
    __asm__ __volatile__("csrwr %0, 0x0" :: "r"((ULONG_PTR)Value) : "memory");
}

FORCEINLINE
ULONG
KiLa64ReadEcfg(VOID)
{
    ULONG Value;

    __asm__ __volatile__("csrrd %0, 0x4" : "=r"(Value));
    return Value;
}

FORCEINLINE
VOID
KiLa64WriteEcfg(_In_ ULONG Value)
{
    __asm__ __volatile__("csrwr %0, 0x4" :: "r"((ULONG_PTR)Value) : "memory");
}

FORCEINLINE
ULONG
KiLa64ReadEstat(VOID)
{
    ULONG Value;

    __asm__ __volatile__("csrrd %0, 0x5" : "=r"(Value));
    return Value;
}

FORCEINLINE
ULONG
KiLa64ReadAsid(VOID)
{
    ULONG Value;

    __asm__ __volatile__("csrrd %0, 0x18" : "=r"(Value));
    return Value & LA64_ASID_MASK;
}

/* dbar 0 orders every earlier load/store against every later one; ibar 0 does
 * the same for instruction fetches. Both are the forms already used by
 * sdk/include/xdk/memaccess.h for this target. */
FORCEINLINE
VOID
KiLa64MemoryBarrier(VOID)
{
    __asm__ __volatile__("dbar 0" ::: "memory");
}

FORCEINLINE
VOID
KiLa64InstructionBarrier(VOID)
{
    __asm__ __volatile__("ibar 0" ::: "memory");
}

/* ---------------------------------------------------------------------------
 * Interrupt state
 * ------------------------------------------------------------------------ */

/* Read and clear CRMD.IE in one csrxchg, [V1-7.4.1]; returns the state that
 * was in effect. Nothing else in CRMD (PLV, DA, PG, DATF, DATM, WE) is
 * touched. Do not use the CRT's _disable(): it writes the whole CRMD with
 * zero, which also clears PG and turns translation off. */
FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ULONG Value = 0;
    ULONG Mask = LA64_CRMD_IE;

    __asm__ __volatile__("csrxchg %0, %1, 0x0"
                         : "+r"(Value)
                         : "r"(Mask)
                         : "memory");
    return (Value & LA64_CRMD_IE) != 0;
}

FORCEINLINE
VOID
KeRestoreInterrupts(_In_ BOOLEAN WereEnabled)
{
    if (WereEnabled)
    {
        ULONG Value = LA64_CRMD_IE;
        ULONG Mask = LA64_CRMD_IE;

        __asm__ __volatile__("csrxchg %0, %1, 0x0"
                             : "+r"(Value)
                             : "r"(Mask)
                             : "memory");
    }
}

/* Mask or unmask one ECFG.LIE line. The interrupt controller line itself
 * stays level-asserted; this only stops the core from taking it. */
FORCEINLINE
VOID
KiLa64SetInterruptLineEnabled(_In_ ULONG Line, _In_ BOOLEAN Enable)
{
    ULONG Value = Enable ? LA64_IRQ_BIT(Line) : 0;
    ULONG Mask = LA64_IRQ_BIT(Line);

    __asm__ __volatile__("csrxchg %0, %1, 0x4"
                         : "+r"(Value)
                         : "r"(Mask)
                         : "memory");
}

/* ---------------------------------------------------------------------------
 * TLB maintenance
 * ------------------------------------------------------------------------ */

/* Local single-page invalidation: the entries matching the given VA that are
 * either global or owned by this address space (op 0x6, [V1-4.2.4.7]). op 0x5
 * would skip the global matches, which is wrong for a page that a kernel
 * mapping may share. Changing a whole hierarchy needs
 * KiLa64FlushCurrentAsidTlb plus a shootdown on the other cores. */
FORCEINLINE
VOID
KeInvalidateTlbEntry(_In_ PVOID Address)
{
    ULONG Asid = KiLa64ReadAsid();

    __asm__ __volatile__("invtlb %0, %1, %2"
                         :: "i"(LA64_INVTLB_ADDR_GLOBAL_OR_ASID),
                            "r"(Asid),
                            "r"((ULONG_PTR)Address)
                         : "memory");
}

/* Drop every entry of the current address space on this core (op 0x4: G=0
 * and ASID == CSR.ASID). Global mappings stay resident, which is what a
 * process teardown or an address-space switch wants; other cores need the
 * shootdown path (KiIpiSendTbFlush).
 *
 * Do not reach for op 0x1 here: the manual defines it as identical to 0x0,
 * so it would also drop the entries of every other ASID on this core. */
FORCEINLINE
VOID
KiLa64FlushCurrentAsidTlb(VOID)
{
    ULONG Asid = KiLa64ReadAsid();

    __asm__ __volatile__("invtlb %0, %1, $zero"
                         :: "i"(LA64_INVTLB_ASID_NONGLOBAL),
                            "r"(Asid)
                         : "memory");
}

/* Drop every entry in this core's TLB, every ASID, global entries included
 * (op 0x0). It is the bring-up / ASID-recycle / machine-check hammer: it also
 * throws away the mappings of address spaces that are still live, so it is
 * not the way to end a single address space. */
FORCEINLINE
VOID
KiLa64InvalidateAllTlb(VOID)
{
    __asm__ __volatile__("invtlb %0, $zero, $zero"
                         :: "i"(LA64_INVTLB_ALL_ENTRIES)
                         : "memory");
}

/* ---------------------------------------------------------------------------
 * Per-CPU state
 *
 * $r21 is the psABI's one non-allocatable GPR ([ABI] lists it as "Reserved
 * (Non-allocatable)"), so the kernel owns it outright and keeps the KPCR base
 * there. The Linux kernel does the same for its percpu base, and the handoff
 * contract fixes it as decision D-4: the entry code sets
 * $r21 = LoaderBlock->Prcb - FIELD_OFFSET(KPCR, Prcb). The NDK already
 * exposes KeGetPcr()/KeGetCurrentPrcb() on top of that register; the
 * accessors below are the IRQL and current-thread fast paths the kernel
 * inlines instead of calling KiGetPcr.
 * ------------------------------------------------------------------------ */

/* Both fast paths reach their KPCR field with a signed 12-bit displacement
 * (ld.d/ld.bu/st.b), so neither field may sit at 2048 or above. */
C_ASSERT(FIELD_OFFSET(KPCR, Prcb.CurrentThread) < 2048);
C_ASSERT(FIELD_OFFSET(KPCR, CurrentIrql) < 2048);

FORCEINLINE
PKTHREAD
_KeGetCurrentThread(VOID)
{
    PKTHREAD Thread;

    /* ld.d's signed 12-bit displacement is why the C_ASSERT above must hold. */
    __asm__ __volatile__("ld.d %0, $r21, %1"
                         : "=r"(Thread)
                         : "i"(FIELD_OFFSET(KPCR, Prcb.CurrentThread))
                         : "memory");
    return Thread;
}

FORCEINLINE
KIRQL
KiLa64GetCurrentIrql(VOID)
{
    ULONG Irql;

    __asm__ __volatile__("ld.bu %0, $r21, %1"
                         : "=r"(Irql)
                         : "i"(FIELD_OFFSET(KPCR, CurrentIrql))
                         : "memory");
    return (KIRQL)Irql;
}

FORCEINLINE
VOID
KiLa64SetCurrentIrql(_In_ KIRQL Irql)
{
    __asm__ __volatile__("st.b %0, $r21, %1"
                         :
                         : "r"((ULONG_PTR)Irql), "i"(FIELD_OFFSET(KPCR, CurrentIrql))
                         : "memory");
}

#define KeGetCurrentIrql KiLa64GetCurrentIrql

/* ---------------------------------------------------------------------------
 * Context and trap-frame accessors
 * ------------------------------------------------------------------------ */

FORCEINLINE
ULONG_PTR
KiGetLinkedTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return (ULONG_PTR)TrapFrame->PreviousTrapFrame;
}

FORCEINLINE
ULONG_PTR
KeGetContextPc(_In_ PCONTEXT Context)
{
    return (ULONG_PTR)Context->Pc;
}

FORCEINLINE
VOID
KeSetContextPc(_Inout_ PCONTEXT Context, _In_ ULONG_PTR ProgramCounter)
{
    Context->Pc = ProgramCounter;
}

/* r4/$a0 carries the integer return value, [ABI]. */
FORCEINLINE
ULONG_PTR
KeGetContextReturnRegister(_In_ PCONTEXT Context)
{
    return (ULONG_PTR)Context->A0;
}

FORCEINLINE
VOID
KeSetContextReturnRegister(_Inout_ PCONTEXT Context, _In_ ULONG_PTR ReturnValue)
{
    Context->A0 = ReturnValue;
}

/* Service arguments past the first two that a user-mode exception carries;
 * r7/r8 are a3/a4 of the integer argument bank [ABI]. kd64 reads the
 * component id and level of a DbgPrint-style exception through these. */
FORCEINLINE
ULONG_PTR
KeGetContextServiceArgument3(_In_ PCONTEXT Context)
{
    return Context->A3;
}

FORCEINLINE
ULONG_PTR
KeGetContextServiceArgument4(_In_ PCONTEXT Context)
{
    return Context->A4;
}

FORCEINLINE
ULONG_PTR
KeGetContextStackRegister(_In_ PCONTEXT Context)
{
    return (ULONG_PTR)Context->Sp;
}

/* r22 (also $s9) is the psABI frame pointer [ABI]. The [fp-8] = saved $ra,
 * [fp-16] = caller's $fp layout is what GCC's LoongArch prologue emits when a
 * frame pointer is forced; it is a compiler convention the unwinder may rely
 * on, not something the psABI text fixes (the ABI only requires 128-bit stack
 * alignment at entry and downward growth). */
FORCEINLINE
ULONG_PTR
KeGetContextFrameRegister(_In_ PCONTEXT Context)
{
    return (ULONG_PTR)Context->Fp;
}

FORCEINLINE
VOID
KeSetContextFrameRegister(_Inout_ PCONTEXT Context, _In_ ULONG_PTR Frame)
{
    Context->Fp = Frame;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameStackRegister(_In_ PKTRAP_FRAME TrapFrame)
{
    return (ULONG_PTR)TrapFrame->Context.Sp;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameFrameRegister(_In_ PKTRAP_FRAME TrapFrame)
{
    return (ULONG_PTR)TrapFrame->Context.Fp;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFramePc(_In_ PKTRAP_FRAME TrapFrame)
{
    return (ULONG_PTR)TrapFrame->Context.Pc;
}

FORCEINLINE
BOOLEAN
KeGetTrapFrameInterruptState(_In_ PKTRAP_FRAME TrapFrame)
{
    return (TrapFrame->Prmd & LA64_PRMD_PIE) != 0;
}

/* A CONTEXT carries no CRMD/PRMD: kernel code runs in the canonical upper
 * half and user code in the lower half, split at bit 47. */
#define KiGetContextPreviousMode(Context) \
    (((LONG64)(Context)->Pc < 0) ? KernelMode : UserMode)

/* The interrupted mode comes from PRMD.PPLV (CRMD.PLV is already 0 on
 * entry), [V1-7.4.2]. */
FORCEINLINE
BOOLEAN
KiLa64UserTrap(_In_ PKTRAP_FRAME TrapFrame)
{
    return (TrapFrame->Prmd & LA64_PRMD_PPLV_MASK) == LA64_PRMD_PPLV_USER;
}

#define KiIsUserModeTrap(TrapFrame) KiLa64UserTrap(TrapFrame)

/* Breakpoint encoding: BREAK is a 32-bit instruction and `break 0` is
 * 0x002A0000 [L-INST]. The LoongArch64 KD transport itself is not defined
 * by this header. */
#define KD_BREAKPOINT_TYPE        ULONG
#define KD_BREAKPOINT_SIZE        sizeof(ULONG)
#define KD_BREAKPOINT_VALUE       0x002a0000
#define KD_ASSERT_BREAKPOINT_SIZE KD_BREAKPOINT_SIZE

/* ---------------------------------------------------------------------------
 * Address space
 * ------------------------------------------------------------------------ */

/* loongarch64 has two roots, PGDL for the lower half and PGDH for the upper
 * half, [V1-7.5.5] [V1-7.5.6]. Every kernel VA walks PGDH, so that is the
 * root a crash dump header records here.
 * TODO: MiArchBootRootFrame() in ntos/mm/arch/loongarch64 decides which root
 * a process address space reports; revisit this once that backend lands. */
FORCEINLINE
ULONG64
KiReadDirectoryTableBase(VOID)
{
    ULONG64 Root;

    __asm__ __volatile__("csrrd %0, 0x1a" : "=r"(Root));
    return Root & ~0xFFFULL;
}

/* ---------------------------------------------------------------------------
 * Interrupt and DPC bookkeeping shared with the generic kernel
 * ------------------------------------------------------------------------ */

FORCEINLINE
BOOLEAN
_KeIsExecutingDpc(VOID)
{
    BOOLEAN Enabled = KeDisableInterrupts();
    BOOLEAN Active = KeGetCurrentPrcb()->DpcRoutineActive;

    /* A migration between the PCR lookup and the flag load would report
     * another processor's DPC state to an ordinary thread. */
    KeRestoreInterrupts(Enabled);
    return Active;
}

#define KI_DPC_NORMAL_PROCESSING_REQUESTED 0x00000002L
#define KI_DPC_NORMAL_DPC_PRESENT          0x00000010L
#define KI_DPC_THREAD_ACTIVE               0x00010000L
#define KI_DPC_THREAD_REQUESTED            0x00020000L

FORCEINLINE
BOOLEAN
KiIsDpcInterruptRequested(
    _In_ PKPRCB Prcb)
{
    return (Prcb->DpcRequestSummary & KI_DPC_NORMAL_PROCESSING_REQUESTED) != 0;
}

FORCEINLINE
VOID
KiSetDpcInterruptRequested(
    _Inout_ PKPRCB Prcb)
{
    InterlockedOr(&Prcb->DpcRequestSummary, KI_DPC_NORMAL_PROCESSING_REQUESTED);
}

FORCEINLINE
VOID
KiClearDpcInterruptRequested(
    _Inout_ PKPRCB Prcb)
{
    InterlockedAnd(&Prcb->DpcRequestSummary, ~KI_DPC_NORMAL_PROCESSING_REQUESTED);
}

FORCEINLINE
VOID
KiSetDpcPresent(
    _Inout_ PKPRCB Prcb)
{
    InterlockedOr(&Prcb->DpcRequestSummary, KI_DPC_NORMAL_DPC_PRESENT);
}

FORCEINLINE
VOID
KiClearDpcRequestState(
    _Inout_ PKPRCB Prcb)
{
    InterlockedAnd(&Prcb->DpcRequestSummary, ~(KI_DPC_NORMAL_PROCESSING_REQUESTED | KI_DPC_NORMAL_DPC_PRESENT));
}

FORCEINLINE
BOOLEAN
KiIsDpcThreadActive(
    _In_ PKPRCB Prcb)
{
    return (Prcb->DpcRequestSummary & KI_DPC_THREAD_ACTIVE) != 0;
}

FORCEINLINE
BOOLEAN
KiIsDpcThreadRequested(
    _In_ PKPRCB Prcb)
{
    return (Prcb->DpcRequestSummary & KI_DPC_THREAD_REQUESTED) != 0;
}

FORCEINLINE
VOID
KiSetDpcThreadActive(
    _Inout_ PKPRCB Prcb)
{
    InterlockedOr(&Prcb->DpcRequestSummary, KI_DPC_THREAD_ACTIVE);
}

FORCEINLINE
VOID
KiSetDpcThreadRequested(
    _Inout_ PKPRCB Prcb)
{
    InterlockedOr(&Prcb->DpcRequestSummary, KI_DPC_THREAD_REQUESTED);
}

FORCEINLINE
VOID
KiClearDpcThreadState(
    _Inout_ PKPRCB Prcb)
{
    InterlockedAnd(&Prcb->DpcRequestSummary, ~(KI_DPC_THREAD_ACTIVE | KI_DPC_THREAD_REQUESTED));
}

#define KiPrcbDpcTimeCount(Prcb) ((Prcb)->DpcTimeCount)
#define KiPrcbDpcTimeLimit(Prcb) ((Prcb)->DpcTimeLimitTicks)
#define KiPrcbDpcWatchdogCount(Prcb) ((Prcb)->DpcWatchdogCount)
#define KiPrcbDpcWatchdogPeriod(Prcb) ((Prcb)->DpcWatchdogPeriodTicks)
#define KiClockTickSuspended(Prcb) ((Prcb)->Sleeping != FALSE)

/* The crash and debugger paths publish one CONTEXT per processor; pcr.c
 * points the slot at Prcb->ProcessorState.ContextFrame. */
extern PCONTEXT KiProcessorContext[MAXIMUM_PROCESSORS];
#define KiPrcbContext(Prcb) (KiProcessorContext[(Prcb)->Number])
/* TODO: this processor block has no type, vendor or context fields, so the debugger data names none */
#define KI_PRCB_CPU_TYPE_OFFSET 0
#define KI_PRCB_VENDOR_STRING_OFFSET 0
#define KI_PRCB_CONTEXT_OFFSET 0

/* The timer queue is a plain ordered list here, so a timer needs no
 * structure inserted anywhere when it is armed. There is no per-processor
 * run-time accounting to refresh beyond KiUpdateRunTime. */
#define KiClockTimerInserted(DueTime) ((VOID)(DueTime))
#define KiRefreshProcessorRunTime(Prcb) ((VOID)(Prcb))

/* The clock snapshots the performance counter together with every tick it
 * writes. The remainder the other ports carry for a scaled counter is zero
 * here: RDTIME.D counts at a fixed frequency, so the counter is the time. */
#define KiQueryClockTickBaseline(Counter, Remainder) \
    (*(Counter) = KeQueryPerformanceCounter(NULL), *(Remainder) = 0)

#define KeGetContextSwitches(Prcb) ((Prcb)->KeContextSwitches)

/* clockint.c defines this and clear the generic macro first; the declaration
 * is what the power and process-query paths call. */
extern NTKERNELAPI volatile KSYSTEM_TIME KeTickCount;
NTKERNELAPI VOID NTAPI KeQueryTickCount(_Out_ PLARGE_INTEGER CurrentCount);

/* TODO: the constant timer (RDTIME/TCFG, [V1-7.6]) is the only architectural
 * time source; measure the per-core frequency once the LoongArch64 HAL
 * calibrates it, until then report Prcb->MHz as the other ports do. */
#define KiQueryEffectiveProcessorMhz(Number) (KiProcessorBlock[(Number)]->MHz)
#define Ki386PerfEnd()

/* ---------------------------------------------------------------------------
 * Exception-frame policy
 *
 * The trap frame holds every GPR and the mode CSRs, so nothing is left for a
 * separate exception frame.
 * ------------------------------------------------------------------------ */

#define KI_NO_EXCEPTION_FRAME

/* The frame the user-mode exception dispatcher runs on. KiDispatchException
 * copies this record below the interrupted $sp and enters ntdll with
 * $a0 = &ExceptionRecord, $a1 = &Context, $ra = 0; the dispatcher owns the
 * stack from there, so it is 16-byte aligned like every psABI call frame.
 * There is no separate machine frame: every register of the interrupted
 * context is in CONTEXT, which is the whole of what the handler restores. */
typedef struct DECLSPEC_ALIGN(16) _KUSER_EXCEPTION_STACK
{
    CONTEXT Context;
    EXCEPTION_RECORD ExceptionRecord;
} KUSER_EXCEPTION_STACK, *PKUSER_EXCEPTION_STACK;

C_ASSERT(FIELD_OFFSET(KUSER_EXCEPTION_STACK, Context) == 0);
C_ASSERT((FIELD_OFFSET(KUSER_EXCEPTION_STACK, ExceptionRecord) & 15) == 0);
C_ASSERT((sizeof(KUSER_EXCEPTION_STACK) & 15) == 0);

/* Without a 128-bit compare-exchange, user-mode SList pops run under a lock:
 * ntdll has no pop sequence for the fault handler to roll back. */
#define KI_USER_SLIST_POP_NO_ROLLBACK

typedef struct _KI_SERVICE_EXCEPTION_STATE
{
    UCHAR Reserved;
} KI_SERVICE_EXCEPTION_STATE, *PKI_SERVICE_EXCEPTION_STATE;

FORCEINLINE
PKEXCEPTION_FRAME
KiEnterServiceException(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PKI_SERVICE_EXCEPTION_STATE State)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(State);
    Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
    return ExceptionFrame;
}

FORCEINLINE
VOID
KiAbortServiceException(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKI_SERVICE_EXCEPTION_STATE State)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    UNREFERENCED_PARAMETER(State);
}

FORCEINLINE
VOID
KiLeaveServiceException(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(TrapFrame);
}

/* ---------------------------------------------------------------------------
 * Kernel entry points the LoongArch64 ke and mm ports provide
 * ------------------------------------------------------------------------ */

#ifdef __cplusplus
extern "C" {
#endif

/* The XDK supplies an inline KeGetCurrentProcessorNumber only for non-kernel
 * builds; the kernel calls the vector-backed one. KeGetCurrentProcessorIndex
 * stays the XDK inline for every build. */
#if defined(_NTOSKRNL_)
ULONG NTAPI KeGetCurrentProcessorNumber(VOID);
#endif
PKTRAP_FRAME NTAPI KeGetTrapFrame(_In_ PKTHREAD Thread);
PKEXCEPTION_FRAME NTAPI KeGetExceptionFrame(_In_ PKTHREAD Thread);
VOID NTAPI KeFlushProcessTb(VOID);
VOID NTAPI KeSweepICache(_In_opt_ PVOID BaseAddress, _In_ SIZE_T FlushSize);
VOID NTAPI KiIpiSendTbFlush(KAFFINITY Targets, PVOID Address, ULONG Pages);
VOID NTAPI KiIpiProcessRequests(VOID);
NTHALAPI NTSTATUS NTAPI HalAllocateAdapterChannel(PADAPTER_OBJECT AdapterObject, PWAIT_CONTEXT_BLOCK Wcb,
                                                  ULONG NumberOfMapRegisters, PDRIVER_CONTROL ExecutionRoutine);
VOID NTAPI KiRundownThread(_In_ PKTHREAD Thread);
DECLSPEC_NORETURN VOID KiExceptionExit(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame);
DECLSPEC_NORETURN VOID NTAPI KiThreadStartup(VOID);
BOOLEAN NTAPI KiSwapContextResume(_In_ BOOLEAN ApcBypass, _In_ PKTHREAD OldThread, _In_ PKTHREAD NewThread);
BOOLEAN NTAPI KiLa64InitializeTrapVector(VOID);
VOID NTAPI KiLa64TrapEntry(VOID);
VOID NTAPI KiLa64TrapHandler(_Inout_ PKTRAP_FRAME TrapFrame);
VOID NTAPI KiLa64InterruptDispatch(_Inout_ PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID NTAPI KiLa64RestoreTrapFrame(PKTRAP_FRAME Frame);
DECLSPEC_NORETURN VOID NTAPI KiLa64ReturnToUser(PKTRAP_FRAME Frame);
DECLSPEC_NORETURN VOID NTAPI KiLa64ServiceExit(PKTRAP_FRAME Frame);

#ifdef __cplusplus
}
#endif

/* ---------------------------------------------------------------------------
 * Not ported from internal/riscv64/ke.h, and why
 *
 * - SBI / firmware feature enumeration and the KI_RISCV_PROCESSOR_FEATURES
 *   record: LoongArch64 describes itself through CPUCFG words
 *   ([3A5000], CPUCFG2 feature bit list in loongarch-abi-notes.md §8), and no
 *   kernel consumer exists yet outside the RISC-V port. The shape of that
 *   record is a LoongArch64 decision this header cannot make.
 * - LSX/LASX and LBT thread state: KSPECIAL_REGISTERS and CONTEXT already fix
 *   the save shapes; the enable/disable policy (EUEN.FPE/SXE/ASXE/BTE,
 *   [V1-7.4.3]) belongs to the ke port.
 * - The RISC-V PLIC, SBI console and remote-fence bridges have no LoongArch64
 *   equivalent in this tree yet; the HAL that owns those lines is not written.
 * ------------------------------------------------------------------------ */
