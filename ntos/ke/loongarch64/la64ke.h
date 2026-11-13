/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     What the LoongArch64 ke files say to each other, plus the
 *              architecture facts the LoongArch64 ke and HAL agree on
 *
 * internal/loongarch64/ke.h is the published kernel-private interface and is
 * not duplicated here. This header only adds:
 *
 *   - the HAL entry points the LoongArch64 ke calls, with the exact
 *     prototypes the HAL implements (agreed with the HAL owner);
 *   - the CSR accessors ke.h does not carry (EUEN, TICLR, TCFG/TVAL,
 *     EENTRY/TLBRENTRY, the ECFG line mask update, the ESTAT software
 *     interrupt bits, RDTIME and CPUCFG);
 *   - the ESTAT decode the dispatch in interrupt.c and trap.c share;
 *   - the per-processor feature record features.c builds and kiinit.c
 *     reports.
 *
 * Every CSR field used here cites the manual volume and section.
 */

#pragma once

/* ---------------------------------------------------------------------------
 * Interrupt line and IRQL policy (shared with the HAL)
 *
 * The HAL wires the external controller's groups to HWI0-HWI7 and reports a
 * vector's IRQL as 4 + (Vector / 32), so group n runs at IRQL 4+n. Clock,
 * IPI and the software lines keep the NDK ladder (sdk/include/xdk/
 * loongarch64/ke.h): APC_LEVEL 1 on SWI0, DISPATCH_LEVEL 2 on SWI1,
 * CLOCK_LEVEL 13 on TI, IPI_LEVEL 14 on IPI, PROFILE_LEVEL 15 on PMCOV.
 * [V1-6.1.1] names the lines, [V1-6.1.2] orders them by int number.
 * ------------------------------------------------------------------------ */
#define LA64_IRQ_TO_IRQL(Line)          ((KIRQL)(4 + ((Line) / 32)))
#define LA64_SWI_APC_LINE               LA64_IRQ_SWI0
#define LA64_SWI_DPC_LINE               LA64_IRQ_SWI1
#define LA64_SPURIOUS_SOURCE            0xFFFFFFFFUL

/* [V1-6.1.1]: exactly 13 line interrupts are recorded, int numbers 0..12. */
#define LA64_LINE_COUNT                 13

/* ---------------------------------------------------------------------------
 * ESTAT decode, [V1-7.4.6] Table 21
 *
 *   Ecode    21:16   the exception or interrupt class
 *   EsubCode 30:22   sub-code for ADEF/ADEM, FPE/VFPE, WPEF/WPEM, GCSC/GCHC
 *   IS       12:0    pending line interrupts
 *
 * Ecode 0 is "INT" and only exists when ECFG.VS=0, which is the only mode
 * this port uses ([V1-6.3.1]: with VS=0 all general exceptions and
 * interrupts share one entry and software dispatches on Ecode and IS).
 * ------------------------------------------------------------------------ */
#define LA64_ESTAT_ECODE_SHIFT          16
#define LA64_ESTAT_ECODE_MASK           0x0000003Fu
#define LA64_ESTAT_ESUBCODE_SHIFT       22
#define LA64_ESTAT_ESUBCODE_MASK        0x000001FFu

#define LA64_ECODE_INT                  0x00
#define LA64_ECODE_PIL                  0x01    /* page invalid, load */
#define LA64_ECODE_PIS                  0x02    /* page invalid, store */
#define LA64_ECODE_PIF                  0x03    /* page invalid, fetch */
#define LA64_ECODE_PME                  0x04    /* page modification */
#define LA64_ECODE_PNR                  0x05    /* page non-readable */
#define LA64_ECODE_PNX                  0x06    /* page non-executable */
#define LA64_ECODE_PPI                  0x07    /* page privilege illegal */
#define LA64_ECODE_ADEF                 0x08    /* address error, fetch / memory */
#define LA64_ECODE_ALE                  0x09    /* alignment fault */
#define LA64_ECODE_BCE                  0x0a    /* bound check */
#define LA64_ECODE_SYS                  0x0b    /* syscall */
#define LA64_ECODE_BRK                  0x0c    /* breakpoint */
#define LA64_ECODE_INE                  0x0d    /* instruction non-defined */
#define LA64_ECODE_IPE                  0x0e    /* instruction privilege error */
#define LA64_ECODE_FPD                  0x0f    /* floating point disabled */
#define LA64_ECODE_SXD                  0x10    /* LSX disabled */
#define LA64_ECODE_ASXD                 0x11    /* LASX disabled */
#define LA64_ECODE_FPE                  0x12    /* floating point error / VFPE */
#define LA64_ECODE_WPE                  0x13    /* watchpoint fetch / memory */
#define LA64_ECODE_BTD                  0x14    /* LBT disabled */
#define LA64_ECODE_BTE                  0x15    /* binary translation */
#define LA64_ECODE_GSPR                 0x16    /* guest sensitive privileged */
#define LA64_ECODE_HVC                  0x17    /* hypervisor call */

FORCEINLINE
ULONG
KiLa64EstatEcode(
    _In_ ULONG Estat)
{
    return (Estat >> LA64_ESTAT_ECODE_SHIFT) & LA64_ESTAT_ECODE_MASK;
}

FORCEINLINE
ULONG
KiLa64EstatEsubCode(
    _In_ ULONG Estat)
{
    return (Estat >> LA64_ESTAT_ESUBCODE_SHIFT) & LA64_ESTAT_ESUBCODE_MASK;
}

FORCEINLINE
ULONG
KiLa64EstatLines(
    _In_ ULONG Estat)
{
    return Estat & LA64_ESTAT_IS_MASK;
}

/* ---------------------------------------------------------------------------
 * CSR accessors ke.h does not provide
 * ------------------------------------------------------------------------ */

/* [V1-7.4.3] EUEN: FPE 0, SXE 1, ASXE 2, BTE 3. */
#define LA64_EUEN_SXE                   0x00000002u
#define LA64_EUEN_ASXE                  0x00000004u

FORCEINLINE
ULONG
KiLa64ReadEuen(VOID)
{
    ULONG Value;

    __asm__ __volatile__("csrrd %0, 0x2" : "=r"(Value));
    return Value;
}

/* [V1-7.6.2] TCFG: En 0, Periodic 1, InitVal n-1:2 (an integer multiple of
 * 4; the hardware appends two zero bits). [V1-7.6.3] TVAL: the current count,
 * read-only. [V1-7.6.5] TICLR: bit 0 is write-1-to-clear and reads 0. */
#define LA64_TCFG_EN                    0x00000001u
#define LA64_TCFG_PERIODIC              0x00000002u

/* [V1-7.4.2] PRMD is loaded into CRMD.PLV/IE/PWE by ERTN, so a user return
 * publishes its mode here before the frame is restored. Only the three
 * defined fields are written; the rest of the CSR is reserved. */
#define LA64_PRMD_FIELDS (LA64_PRMD_PPLV_MASK | LA64_PRMD_PIE | 0x00000008u)

FORCEINLINE
VOID
KiLa64WritePrmd(
    _In_ ULONG Value)
{
    ULONG Current = Value & LA64_PRMD_FIELDS;
    ULONG Mask = LA64_PRMD_FIELDS;

    __asm__ __volatile__("csrxchg %0, %1, 0x1"
                         : "+r"(Current)
                         : "r"(Mask)
                         : "memory");
}

FORCEINLINE
VOID
KiLa64ClearTimerInterrupt(VOID)
{
    /* csrwr writes the value and discards the read-back, which is always 0. */
    __asm__ __volatile__("csrwr %0, 0x44" :: "r"((ULONG_PTR)1) : "memory");
}

FORCEINLINE
ULONG64
KiLa64ReadStableCounter(VOID)
{
    ULONG64 Value;

    /* RDTIME.D rd, rj: the counter named by rj is read into rd. Timer 0 is
     * the core's own stable counter, [V1-7.6.1]; $r0 means "no timer id
     * register is written back". */
    __asm__ __volatile__("rdtime.d %0, %1" : "=r"(Value) : "r"(0ULL) : "memory");
    return Value;
}

FORCEINLINE
ULONG
KiLa64ReadCpucfg(
    _In_ ULONG Word)
{
    ULONG Value;

    __asm__ __volatile__("cpucfg %0, %1" : "=r"(Value) : "r"(Word) : "memory");
    return Value;
}

/* [V1-7.4.10] EENTRY: the base of the general exception entry, a virtual
 * address whose bits 11:0 are read-only zero. [V1-7.5.11] TLBRENTRY: the TLB
 * refill entry, a PHYSICAL address; the refill runs with DA=1, PG=0. */
FORCEINLINE
ULONG64
KiLa64ReadEentry(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0xc" : "=r"(Value));
    return Value;
}

FORCEINLINE
VOID
KiLa64WriteEentry(
    _In_ ULONG64 Value)
{
    __asm__ __volatile__("csrwr %0, 0xc" :: "r"(Value) : "memory");
}

FORCEINLINE
ULONG64
KiLa64ReadTlbRefillEntry(VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("csrrd %0, 0x88" : "=r"(Value));
    return Value;
}

FORCEINLINE
VOID
KiLa64WriteTlbRefillEntry(
    _In_ ULONG64 Value)
{
    __asm__ __volatile__("csrwr %0, 0x88" :: "r"(Value) : "memory");
}

/* Fill one STLB entry ([V1-7.5.1]-[V1-7.5.3], [V1-4.2.4.4]).
 *
 *   Vppn  TLBEHI.VPPN, i.e. VA[VALEN-1:13]: the pair's virtual page number
 *         with the even/odd selector bit dropped ([V1-5.4.2]).
 *   Lo0    TLBELO0: the even page of the pair.
 *   Lo1    TLBELO1: the odd page.
 *
 * TLBIDX is written with NE=0 (an entry that matches and is not "invalid
 * entry") and PS=12 (log2 of 4 KiB, [V1-7.5.10]); the slot is chosen by the
 * implementation, [V1-4.2.4.4]. Per-half validity is TLBELO.V. */
FORCEINLINE
VOID
KiLa64FillTlbPair(
    _In_ ULONG64 Vppn,
    _In_ ULONG64 Lo0,
    _In_ ULONG64 Lo1)
{
    ULONG64 TlbIdx = (ULONG64)PAGE_SHIFT << 24;   /* STLBPS.PS field is 24:29 */

    __asm__ __volatile__("csrwr %0, 0x11" :: "r"(Vppn) : "memory");
    __asm__ __volatile__("csrwr %0, 0x12" :: "r"(Lo0) : "memory");
    __asm__ __volatile__("csrwr %0, 0x13" :: "r"(Lo1) : "memory");
    __asm__ __volatile__("csrwr %0, 0x10" :: "r"(TlbIdx) : "memory");
    __asm__ __volatile__("tlbfill" ::: "memory");
}

/* ESTAT.IS[1:0] are the only writable interrupt-status bits, [V1-6.1.1]:
 * software writes 1 to raise a software interrupt and 0 to clear it. The
 * other bits are hardware-owned and are not in the masks below. */
FORCEINLINE
VOID
KiLa64RequestSoftwareLine(
    _In_ ULONG Line)
{
    ULONG Value = LA64_IRQ_BIT(Line);
    ULONG Mask = LA64_IRQ_BIT(Line);

    ASSERT((Line == LA64_IRQ_SWI0) || (Line == LA64_IRQ_SWI1));
    __asm__ __volatile__("csrxchg %0, %1, 0x5"
                         : "+r"(Value)
                         : "r"(Mask)
                         : "memory");
}

FORCEINLINE
VOID
KiLa64ClearSoftwareLine(
    _In_ ULONG Line)
{
    ULONG Value = 0;
    ULONG Mask = LA64_IRQ_BIT(Line);

    ASSERT((Line == LA64_IRQ_SWI0) || (Line == LA64_IRQ_SWI1));
    __asm__ __volatile__("csrxchg %0, %1, 0x5"
                         : "+r"(Value)
                         : "r"(Mask)
                         : "memory");
}

/* Replace only ECFG.LIE[12:0]; ECFG.VS and the compatibility bits the loader
 * may have left are preserved ([V1-7.4.5]: LIE 12:0, VS 18:16). */
FORCEINLINE
VOID
KiLa64SetInterruptMask(
    _In_ ULONG Lines)
{
    ULONG Value = Lines & LA64_ECFG_LIE_MASK;

    __asm__ __volatile__("csrxchg %0, %1, 0x4"
                         : "+r"(Value)
                         : "r"(LA64_ECFG_LIE_MASK)
                         : "memory");
}

/* ---------------------------------------------------------------------------
 * HAL interface (implemented by hal/loongarch64, prototype-agreed)
 *
 * The HAL owns the external controller, the IPI block and the stable timer's
 * rearm; ke owns the IRQL ladder, the ECFG line mask and CRMD.IE.
 * ------------------------------------------------------------------------ */
VOID NTAPI HalpLa64ClockInterrupt(_In_ PKTRAP_FRAME TrapFrame);
VOID NTAPI HalpLa64SuspendClockTick(VOID);
ULONG NTAPI HalpLa64ResumeClockTick(VOID);
ULONG NTAPI HalpLa64ClaimInterrupt(VOID);
VOID NTAPI HalpLa64CompleteInterrupt(_In_ ULONG Vector);
VOID NTAPI HalpLa64IpiSend(_In_ KAFFINITY Targets);
VOID NTAPI HalpLa64IpiAcknowledge(VOID);
ULONG NTAPI HalpLa64QueryProcessorId(_In_ ULONG ProcessorNumber);

/* ---------------------------------------------------------------------------
 * Per-processor identity, filled once from CPUCFG and the loader block
 * ------------------------------------------------------------------------ */
typedef struct _KI_LA64_PROCESSOR
{
    BOOLEAN Valid;
    ULONG CpuId;                /* CSR.CPUID, [V1-7.4.12] */
    ULONG ProcessorId;          /* CPUCFG0 PRId, 3A5000 manual Table 54 */
    ULONG CpuCfg1;              /* ARCH/PALEN/VALEN/UAL/RI/EP/RPLV/HP */
    ULONG CpuCfg2;              /* FP/LSX/LASX/LLFTP/LSPW/LAM */
    ULONG CpuCfg3;              /* CCDMA/SFB/ITLBT/ICACHET/SPW_LVL/RVA */
    ULONG CpuCfg4;              /* crystal clock in Hz, 3A5000 manual 6.1.2 */
    ULONG CpuCfg5;              /* [31:16] CC_DIV, [15:0] CC_MUL */
    ULONG64 CounterFrequency;   /* CPUCFG4 * CC_MUL / CC_DIV, or 0 */
    ULONG Dmw[4];
} KI_LA64_PROCESSOR, *PKI_LA64_PROCESSOR;

extern KI_LA64_PROCESSOR KiLa64Processor;

/* CPUCFG1 fields, 3A5000 manual Table 54 (word 1). */
#define LA64_CPUCFG1_ARCH_MASK      0x00000003u
#define LA64_CPUCFG1_ARCH_LA64      0x00000002u
#define LA64_CPUCFG1_PALEN_SHIFT    4
#define LA64_CPUCFG1_PALEN_MASK     0x000000FFu
#define LA64_CPUCFG1_VALEN_SHIFT    12
#define LA64_CPUCFG1_VALEN_MASK     0x000000FFu

/* CPUCFG2 features, 3A5000 manual Table 54 (word 2). */
#define LA64_CPUCFG2_FP             0x00000001u
#define LA64_CPUCFG2_FP_SP          0x00000002u
#define LA64_CPUCFG2_FP_DP          0x00000004u
#define LA64_CPUCFG2_LSX            0x00000040u
#define LA64_CPUCFG2_LASX           0x00000080u
#define LA64_CPUCFG2_LLFTP          0x00004000u

/* CPUCFG3 features, 3A5000 manual Table 54 (word 3). Bit 7 ICACHET means the
 * core maintains I-cache/D-cache coherency in hardware, which is what lets
 * KeSweepICache be an ibar instead of a CACOP sweep. */
#define LA64_CPUCFG3_ICACHET        0x00000080u

/* CPUCFG10 cache presence bits (3A5000 manual Table 54, word 0x10) and the
 * Linesize-log2 field shared by CPUCFG11-14 (bits 30:24). */
#define LA64_CPUCFG10_L1IU_PRESENT  0x00000001u
#define LA64_CPUCFG10_L1D_PRESENT   0x00000004u
#define LA64_CPUCFG_LINESIZE_SHIFT  24
#define LA64_CPUCFG_LINESIZE_MASK   0x0000007Fu

/* ---------------------------------------------------------------------------
 * Entry points this directory provides or needs from its own files
 * ------------------------------------------------------------------------ */
#ifdef __cplusplus
extern "C" {
#endif

/* trap.S */
VOID NTAPI KiLa64TrapEntry(VOID);
VOID NTAPI KiLa64TlbRefillEntry(VOID);
VOID NTAPI KiLa64RestoreTrapFrame(PKTRAP_FRAME Frame);

/* trap.c */
DECLSPEC_NORETURN VOID NTAPI KiLa64TrapStop(_In_ PKTRAP_FRAME TrapFrame);
VOID NTAPI KiLa64TlbRefillHandler(_In_ PKTRAP_FRAME TrapFrame);

/* thrdini.c builds the initial frames; it also runs the idle loop. */
VOID NTAPI KiLa64StartUserThread(VOID) DECLSPEC_NORETURN;

/* stack.c */
PVOID NTAPI KiLa64PrepareKernelStack(
    _In_ PVOID StackBase,
    _In_ PVOID StackLimit,
    _In_ ULONG_PTR StackPointer,
    _In_ ULONG_PTR FramePointer);

/* pcr.c */
BOOLEAN NTAPI KiLa64InitializeBootPcr(
    _Out_ PKPCR Pcr,
    _In_ PKTHREAD Thread,
    _In_ ULONG_PTR CpuId,
    _In_ PVOID DpcStack);
VOID NTAPI KiLa64InitializePcr(
    _Inout_ PKPCR Pcr,
    _In_ PKTHREAD Thread,
    _In_ ULONG_PTR CpuId,
    _In_ ULONG Number,
    _In_ PVOID DpcStack,
    _In_ PVOID PanicStack);
VOID NTAPI KiLa64JoinSchedulerSubNode(_In_ PKPRCB Prcb);

/* kiinit.c */
DECLSPEC_NORETURN VOID NTAPI KiLa64SystemStartup(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

/* irql.c */
VOID NTAPI KiLa64SynchronizeInterruptMask(VOID);

/* interrupt.c */
BOOLEAN NTAPI KiLa64AllocateInterruptTable(_In_ ULONG Number);
PKINTERRUPT NTAPI KiLa64QueryInterrupt(_In_ ULONG Source);
ULONG NTAPI KiLa64QueryInterruptLimit(VOID);

/* ipi.c */
VOID NTAPI KiLa64SendSoftwareInterrupt(_In_ KAFFINITY TargetSet, _In_ KIRQL Irql);

/* freeze.c / ctxswitch.S: declared for this port only, like the other
 * architecture headers do. */
BOOLEAN KiProcessorFreezeHandler(_In_ PKTRAP_FRAME TrapFrame, _In_ PKEXCEPTION_FRAME ExceptionFrame);
VOID NTAPI KiRetireDpcListInDpcStack(_In_ PKPRCB Prcb, _In_ PVOID DpcStack);

/* usercopy.c: the architecture's user-copy primitives. */
NTSTATUS NTAPI KiLa64CopyFromUser(_Out_writes_bytes_(Length) PVOID Destination, _In_ const VOID *Source, _In_ SIZE_T Length);
NTSTATUS NTAPI KiLa64CopyToUser(_Out_ PVOID Destination, _In_reads_bytes_(Length) const VOID *Source, _In_ SIZE_T Length);
NTSTATUS NTAPI KiLa64ReadMemory(_Out_writes_bytes_(Length) PVOID Destination, _In_ const VOID *Source, _In_ SIZE_T Length);
NTSTATUS NTAPI KiLa64ReadCurrentProcess(_In_ PVOID Source, _Out_writes_bytes_(Length) PVOID Destination, _In_ SIZE_T Length, _Out_opt_ PSIZE_T Returned);
BOOLEAN NTAPI KiLa64FixupUserCopy(_Inout_ PKTRAP_FRAME TrapFrame, _In_ NTSTATUS Status);

/* syscall.c */
DECLSPEC_NORETURN VOID NTAPI KiLa64SystemService(_Inout_ PKTRAP_FRAME Frame);

/* usercall.c */
NTSTATUS NTAPI KiLa64UserModeCallout(_Inout_ PKTRAP_FRAME Frame, _Out_ PKCALLOUT_FRAME CalloutFrame);
DECLSPEC_NORETURN VOID NTAPI KiLa64CallbackReturn(_In_ PKCALLOUT_FRAME CalloutFrame, _In_ NTSTATUS Status);

/* features.c */
VOID NTAPI KiLa64IdentifyProcessor(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
VOID NTAPI KiLa64ReportProcessorFeatures(VOID);
VOID NTAPI KiLa64SaveProcessorClock(_In_ PKPRCB Prcb);
ULONG64 NTAPI KiLa64QueryCounterFrequency(VOID);
ULONG NTAPI KiLa64QueryLargestCacheLine(VOID);
ULONG NTAPI KiLa64QueryCacheBlockSize(VOID);

/* stubs.c: the fail-stop used before any console or PCR exists. */
DECLSPEC_NORETURN VOID NTAPI KiLa64Unimplemented(_In_ const CHAR *Routine);

/* console.c: the loader-described early UART. */
VOID NTAPI KiLa64ConsoleInitialize(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
BOOLEAN NTAPI KiLa64ConsoleReady(VOID);
ULONG NTAPI KiLa64ConsoleInterface(VOID);
VOID NTAPI KiLa64ConsolePutByte(_In_ UCHAR Byte);
VOID NTAPI KiLa64ConsoleWrite(_In_reads_bytes_(Length) const CHAR *Buffer, _In_ SIZE_T Length);
BOOLEAN NTAPI KiLa64ConsoleGetByte(_Out_ PUCHAR Byte);

/* viacache.c: cache geometry and DMA maintenance (cache.c). */
BOOLEAN NTAPI KiLa64IsPhysicalCached(_In_ ULONG64 PhysicalAddress);
BOOLEAN NTAPI KiLa64FlushDmaRange(_In_ ULONG64 PhysicalAddress, _In_ SIZE_T Length, _In_ BOOLEAN Invalidate);

#ifdef __cplusplus
}
#endif

/* EOF */
