/*
 * PROJECT:         LiberNT Kernel
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * FILE:            ntoskrnl/ke/arm64/cpu.c
 * PURPOSE:         CPU management stubs for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#if defined(_M_ARM64)
#include <reactos/cpuaudit.h>
#endif
#define NDEBUG
#include <debug.h>

#define ARM64_STUB() UNIMPLEMENTED_DBGBREAK()

#define READ_SYSREG64(_var, _reg) ((_var) = ARM64_READ_SYSREG(_reg))
#define WRITE_SYSREG64(_reg, _val) ARM64_WRITE_SYSREG(_reg, _val)

#define ARM64_MDSCR_SS             (1ULL << 0)
#define ARM64_MDSCR_TDCC           (1ULL << 12)
#define ARM64_MDSCR_KDE            (1ULL << 13)
#define ARM64_MDSCR_MDE            (1ULL << 15)
#define ARM64_PSTATE_SS            (1ULL << 21)
#define ARM64_DEBUG_CONTROL_ENABLE 1UL

#define READ_DBG_SLOT(_array, _slot, _name)                          \
    do                                                               \
    {                                                                \
        ULONGLONG _value;                                            \
        _value =                                                     \
            ARM64_READ_SYSREG(_name##_slot##_EL1);                   \
        ProcessorState->SpecialRegisters._array[_slot] = _value;      \
    } while (0)

#define WRITE_DBG_SLOT(_array, _slot, _name)                         \
    do                                                               \
    {                                                                \
        ULONGLONG _value = ProcessorState->SpecialRegisters._array[_slot]; \
        ARM64_WRITE_SYSREG(_name##_slot##_EL1,                       \
                           _value);                                  \
    } while (0)

ULONG KeFixedTbEntries;
ULONG KiDmaIoCoherency;
ULONG KeIcacheFlushCount;
ULONG KeDcacheFlushCount;
ULONG KeLargestCacheLine = 64;
extern PVOID MmPagedPoolStart;

#define ARM64_MAX_CACHE_LEVELS 7

typedef struct _ARM64_CACHE_SWEEP_LEVEL
{
    ULONG Level;
    ULONG NumSets;
    ULONG NumWays;
    ULONG SetShift;
    ULONG WayShift;
} ARM64_CACHE_SWEEP_LEVEL, *PARM64_CACHE_SWEEP_LEVEL;

static ARM64_CACHE_SWEEP_LEVEL KiArm64DcacheSweepLevels[ARM64_MAX_CACHE_LEVELS];
static ULONG KiArm64DcacheSweepLevelCount;
static volatile LONG KiArm64DcacheSweepGeometryValid;

VOID
NTAPI
KiArm64ReadCcsidr(
    _In_ ULONG LevelIndex,
    _In_ BOOLEAN InstructionSide,
    _Out_ PULONG LineShift,
    _Out_ PULONG Ways,
    _Out_ PULONG Sets)
{
    ULONG64 Csselr = ((ULONG64)LevelIndex << 1) | (InstructionSide ? 1 : 0);
    ULONG64 Ccsidr;

    /* Select the cache; the ISB orders the CSSELR_EL1 write before the
       CCSIDR_EL1 read */
    ARM64_WRITE_SYSREG(CSSELR_EL1, Csselr);
    ARM64_ISB();
    Ccsidr = ARM64_READ_SYSREG(CCSIDR_EL1);

    *LineShift = (ULONG)(Ccsidr & 0x7) + 4;
    *Ways = (ULONG)((Ccsidr >> 3) & 0x3FF) + 1;
    *Sets = (ULONG)((Ccsidr >> 13) & 0x7FFF) + 1;
}

static
VOID
KiArm64EnsureDcacheSweepGeometry(VOID)
{
    ARM64_CACHE_SWEEP_LEVEL LocalLevels[ARM64_MAX_CACHE_LEVELS];
    ULONG LocalCount = 0;
    ULONG LargestLine = 64;
    ULONG64 Clidr;
    ULONG Level;

    if (KiArm64DcacheSweepGeometryValid)
    {
        ARM64_DMB(ISH);
        return;
    }

    Clidr = ARM64_READ_SYSREG(CLIDR_EL1);

    for (Level = 0; Level < ARM64_MAX_CACHE_LEVELS; Level++)
    {
        ULONG CacheType = (Clidr >> (Level * 3)) & 0x7;
        ULONG LineShift;
        ULONG NumWays;
        ULONG NumSets;

        if (CacheType < 2)
        {
            continue;
        }

        KiArm64ReadCcsidr(Level, FALSE, &LineShift, &NumWays, &NumSets);

        LocalLevels[LocalCount].Level = Level;
        LocalLevels[LocalCount].NumSets = NumSets;
        LocalLevels[LocalCount].NumWays = NumWays;
        LocalLevels[LocalCount].SetShift = LineShift;
        LocalLevels[LocalCount].WayShift =
#if defined(_MSC_VER) && !defined(__clang__)
            (NumWays <= 1) ? 32 : (ULONG)_CountLeadingZeros(NumWays - 1);
#else
            (NumWays <= 1) ? 32 : (ULONG)__builtin_clz(NumWays - 1);
#endif
        LocalCount++;

        if ((1UL << LineShift) > LargestLine)
        {
            LargestLine = 1UL << LineShift;
        }
    }

    RtlCopyMemory(KiArm64DcacheSweepLevels,
                  LocalLevels,
                  LocalCount * sizeof(LocalLevels[0]));
    KiArm64DcacheSweepLevelCount = LocalCount;
    KeLargestCacheLine = LargestLine;
    ARM64_DMB(ISH);
    KiArm64DcacheSweepGeometryValid = 1;
}

/*
 * Number of hardware breakpoint and watchpoint registers.
 * Read from ID_AA64DFR0_EL1 during early init.
 * BRPs field (bits 15:12) gives breakpoints-1, WRPs field (bits 23:20) gives watchpoints-1.
 * Default to conservative values (6 breakpoints, 2 watchpoints) until initialized.
 */
ULONG KiArm64NumBreakpoints = 6;
ULONG KiArm64NumWatchpoints = 2;

/*
 * Enable self-hosted debug on the current processor.
 *
 * Firmware can leave either OS debug lock set. While locked, writes to the
 * breakpoint and watchpoint registers are permitted, but the corresponding
 * debug events do not become active. Establish MDSCR_EL1 with TDCC set so EL0
 * cannot access the Debug Communications Channel while all event controls
 * remain disabled. These registers are banked per processor, so this must run
 * during BSP and AP initialization.
 */
VOID
KiInitializeDebugMonitors(VOID)
{
    ULONGLONG Zero = 0;

    WRITE_SYSREG64(OSDLR_EL1, Zero);
    WRITE_SYSREG64(OSLAR_EL1, Zero);
    WRITE_SYSREG64(MDSCR_EL1, ARM64_MDSCR_TDCC);
    ARM64_ISB();
}

/*
 * Initialize the debug register counts by reading ID_AA64DFR0_EL1.
 * Called early during CPU initialization.
 */
VOID
KiInitializeDebugRegisterCounts(VOID)
{
    ULONGLONG Dfr0;

    Dfr0 = ARM64_READ_SYSREG(ID_AA64DFR0_EL1);

    /* BRPs field (bits 15:12): number of breakpoints minus 1 */
    KiArm64NumBreakpoints = ((Dfr0 >> 12) & 0xF) + 1;

    /* WRPs field (bits 23:20): number of watchpoints minus 1 */
    KiArm64NumWatchpoints = ((Dfr0 >> 20) & 0xF) + 1;

    /* Clamp to maximum supported by our structures (8 BPs, 2 WPs) */
    if (KiArm64NumBreakpoints > 8)
        KiArm64NumBreakpoints = 8;
    if (KiArm64NumWatchpoints > 2)
        KiArm64NumWatchpoints = 2;
}

#if DBG
CODE_SEG("INIT")
VOID
KiReportCpuFeatures(IN PKPRCB Prcb)
{
    /* Debug logging removed */
    UNREFERENCED_PARAMETER(Prcb);
}
#endif

VOID
KeFlushTb(VOID)
{
    KiCpuAuditEvent(CPU_AUDIT_TLB, 0, 1, 0);
    ARM64_DSB(ISH);
    ARM64_TLBI_ALL(VMALLE1IS);
    ARM64_DSB(ISH);
    ARM64_ISB();
}

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
    KeFlushTb();
}

static
BOOLEAN
KiArm64PmuImplemented(VOID)
{
    ULONG64 Dfr0;
    ULONG PmuVersion;

    READ_SYSREG64(Dfr0, ID_AA64DFR0_EL1);
    PmuVersion = (ULONG)((Dfr0 >> 8) & 0xF);
    return (PmuVersion != 0) && (PmuVersion != 0xF);
}

VOID
NTAPI
KiSaveProcessorControlState(_Out_ PKPROCESSOR_STATE ProcessorState)
{
    ULONGLONG Value;
    ULONG NumBps, NumWps;

    if (ProcessorState == NULL)
    {
        return;
    }

    READ_SYSREG64(ProcessorState->SpecialRegisters.Elr_El1, ELR_EL1);
    READ_SYSREG64(Value, SPSR_EL1);
    ProcessorState->SpecialRegisters.Spsr_El1 = (ULONG)Value;
    READ_SYSREG64(ProcessorState->SpecialRegisters.Tpidr_El0, TPIDR_EL0);
    READ_SYSREG64(ProcessorState->SpecialRegisters.Tpidrro_El0, TPIDRRO_EL0);
    READ_SYSREG64(ProcessorState->SpecialRegisters.Tpidr_El1, TPIDR_EL1);

    /*
     * Save hardware breakpoint registers. Only access registers that exist
     * according to ID_AA64DFR0_EL1. Accessing non-existent registers causes
     * an undefined instruction exception on some implementations (e.g., QEMU).
     */
    NumBps = KiArm64NumBreakpoints;

    /* Zero out all slots first to ensure clean state */
    RtlZeroMemory(ProcessorState->SpecialRegisters.KernelBvr,
                  sizeof(ProcessorState->SpecialRegisters.KernelBvr));
    RtlZeroMemory(ProcessorState->SpecialRegisters.KernelBcr,
                  sizeof(ProcessorState->SpecialRegisters.KernelBcr));

    /* Save only the breakpoint registers that exist */
    if (NumBps > 0) { READ_DBG_SLOT(KernelBvr, 0, DBGBVR); READ_DBG_SLOT(KernelBcr, 0, DBGBCR); }
    if (NumBps > 1) { READ_DBG_SLOT(KernelBvr, 1, DBGBVR); READ_DBG_SLOT(KernelBcr, 1, DBGBCR); }
    if (NumBps > 2) { READ_DBG_SLOT(KernelBvr, 2, DBGBVR); READ_DBG_SLOT(KernelBcr, 2, DBGBCR); }
    if (NumBps > 3) { READ_DBG_SLOT(KernelBvr, 3, DBGBVR); READ_DBG_SLOT(KernelBcr, 3, DBGBCR); }
    if (NumBps > 4) { READ_DBG_SLOT(KernelBvr, 4, DBGBVR); READ_DBG_SLOT(KernelBcr, 4, DBGBCR); }
    if (NumBps > 5) { READ_DBG_SLOT(KernelBvr, 5, DBGBVR); READ_DBG_SLOT(KernelBcr, 5, DBGBCR); }
    if (NumBps > 6) { READ_DBG_SLOT(KernelBvr, 6, DBGBVR); READ_DBG_SLOT(KernelBcr, 6, DBGBCR); }
    if (NumBps > 7) { READ_DBG_SLOT(KernelBvr, 7, DBGBVR); READ_DBG_SLOT(KernelBcr, 7, DBGBCR); }

    /*
     * Save hardware watchpoint registers. Same principle as breakpoints.
     */
    NumWps = KiArm64NumWatchpoints;

    RtlZeroMemory(ProcessorState->SpecialRegisters.KernelWvr,
                  sizeof(ProcessorState->SpecialRegisters.KernelWvr));
    RtlZeroMemory(ProcessorState->SpecialRegisters.KernelWcr,
                  sizeof(ProcessorState->SpecialRegisters.KernelWcr));

    if (NumWps > 0) { READ_DBG_SLOT(KernelWvr, 0, DBGWVR); READ_DBG_SLOT(KernelWcr, 0, DBGWCR); }
    if (NumWps > 1) { READ_DBG_SLOT(KernelWvr, 1, DBGWVR); READ_DBG_SLOT(KernelWcr, 1, DBGWCR); }

    READ_SYSREG64(ProcessorState->ArchState.Midr_El1, MIDR_EL1);
    READ_SYSREG64(ProcessorState->ArchState.Sctlr_El1, SCTLR_EL1);
    READ_SYSREG64(ProcessorState->ArchState.Actlr_El1, ACTLR_EL1);
    READ_SYSREG64(ProcessorState->ArchState.Cpacr_El1, CPACR_EL1);
    READ_SYSREG64(ProcessorState->ArchState.Tcr_El1, TCR_EL1);
    READ_SYSREG64(ProcessorState->ArchState.Ttbr0_El1, TTBR0_EL1);
    READ_SYSREG64(ProcessorState->ArchState.Ttbr1_El1, TTBR1_EL1);
    READ_SYSREG64(ProcessorState->ArchState.Esr_El1, ESR_EL1);
    READ_SYSREG64(ProcessorState->ArchState.Far_El1, FAR_EL1);
    if (KiArm64PmuImplemented())
    {
        READ_SYSREG64(ProcessorState->ArchState.Pmcr_El0, PMCR_EL0);
        READ_SYSREG64(ProcessorState->ArchState.Pmcntenset_El0, PMCNTENSET_EL0);
        READ_SYSREG64(ProcessorState->ArchState.Pmccntr_El0, PMCCNTR_EL0);
        READ_SYSREG64(ProcessorState->ArchState.Pmovsclr_El0, PMOVSCLR_EL0);
        READ_SYSREG64(ProcessorState->ArchState.Pmselr_El0, PMSELR_EL0);
        READ_SYSREG64(ProcessorState->ArchState.Pmuserenr_El0, PMUSERENR_EL0);
    }
    else
    {
        ProcessorState->ArchState.Pmcr_El0 = 0;
        ProcessorState->ArchState.Pmcntenset_El0 = 0;
        ProcessorState->ArchState.Pmccntr_El0 = 0;
        ProcessorState->ArchState.Pmovsclr_El0 = 0;
        ProcessorState->ArchState.Pmselr_El0 = 0;
        ProcessorState->ArchState.Pmuserenr_El0 = 0;
    }
    READ_SYSREG64(ProcessorState->ArchState.Mair_El1, MAIR_EL1);
    READ_SYSREG64(ProcessorState->ArchState.Vbar_El1, VBAR_EL1);

    RtlZeroMemory(ProcessorState->ArchState.Pmxevcntr_El0,
                  sizeof(ProcessorState->ArchState.Pmxevcntr_El0));
    RtlZeroMemory(ProcessorState->ArchState.Pmxevtyper_El0,
                  sizeof(ProcessorState->ArchState.Pmxevtyper_El0));
}

static
BOOLEAN
KiArm64DebugRegistersEnabled(
    _In_ const KSPECIAL_REGISTERS* Registers,
    _In_ ULONG BreakpointCount,
    _In_ ULONG WatchpointCount)
{
    ULONG Slot;

    for (Slot = 0; Slot < BreakpointCount; Slot++)
    {
        if (Registers->KernelBcr[Slot] & ARM64_DEBUG_CONTROL_ENABLE)
            return TRUE;
    }

    for (Slot = 0; Slot < WatchpointCount; Slot++)
    {
        if (Registers->KernelWcr[Slot] & ARM64_DEBUG_CONTROL_ENABLE)
            return TRUE;
    }

    return FALSE;
}

VOID
NTAPI
KiRestoreProcessorControlState(_In_ PKPROCESSOR_STATE ProcessorState)
{
    ULONGLONG Mdscr = ARM64_MDSCR_TDCC;
    ULONG NumBps, NumWps;

    if (ProcessorState == NULL)
    {
        return;
    }

    WRITE_SYSREG64(TPIDR_EL0, ProcessorState->SpecialRegisters.Tpidr_El0);
    WRITE_SYSREG64(TPIDRRO_EL0, ProcessorState->SpecialRegisters.Tpidrro_El0);
    WRITE_SYSREG64(TPIDR_EL1, ProcessorState->SpecialRegisters.Tpidr_El1);

    /*
     * Restore hardware breakpoint registers. Only access registers that exist
     * according to ID_AA64DFR0_EL1.
     */
    NumBps = KiArm64NumBreakpoints;

    if (NumBps > 0) { WRITE_DBG_SLOT(KernelBvr, 0, DBGBVR); WRITE_DBG_SLOT(KernelBcr, 0, DBGBCR); }
    if (NumBps > 1) { WRITE_DBG_SLOT(KernelBvr, 1, DBGBVR); WRITE_DBG_SLOT(KernelBcr, 1, DBGBCR); }
    if (NumBps > 2) { WRITE_DBG_SLOT(KernelBvr, 2, DBGBVR); WRITE_DBG_SLOT(KernelBcr, 2, DBGBCR); }
    if (NumBps > 3) { WRITE_DBG_SLOT(KernelBvr, 3, DBGBVR); WRITE_DBG_SLOT(KernelBcr, 3, DBGBCR); }
    if (NumBps > 4) { WRITE_DBG_SLOT(KernelBvr, 4, DBGBVR); WRITE_DBG_SLOT(KernelBcr, 4, DBGBCR); }
    if (NumBps > 5) { WRITE_DBG_SLOT(KernelBvr, 5, DBGBVR); WRITE_DBG_SLOT(KernelBcr, 5, DBGBCR); }
    if (NumBps > 6) { WRITE_DBG_SLOT(KernelBvr, 6, DBGBVR); WRITE_DBG_SLOT(KernelBcr, 6, DBGBCR); }
    if (NumBps > 7) { WRITE_DBG_SLOT(KernelBvr, 7, DBGBVR); WRITE_DBG_SLOT(KernelBcr, 7, DBGBCR); }

    /*
     * Restore hardware watchpoint registers.
     */
    NumWps = KiArm64NumWatchpoints;

    if (NumWps > 0) { WRITE_DBG_SLOT(KernelWvr, 0, DBGWVR); WRITE_DBG_SLOT(KernelWcr, 0, DBGWCR); }
    if (NumWps > 1) { WRITE_DBG_SLOT(KernelWvr, 1, DBGWVR); WRITE_DBG_SLOT(KernelWcr, 1, DBGWCR); }

    /*
     * The debug registers above only take effect once MDSCR_EL1 enables monitor
     * debug events (MDE) and debug exceptions at EL1 (KDE). Start from the
     * OS-owned baseline and derive the dynamic bits from the state being
     * restored. This also avoids a trapped read of MDSCR_EL1 during external
     * debugging; TDCC keeps EL0 access to the Debug Communications Channel
     * disabled.
     *
     * ContextFrame is authoritative for exception return: a debugger can modify
     * its CPSR after KiSaveProcessorControlState captured Spsr_El1, and
     * KiTrapReturn later copies that CPSR to SPSR_EL1.
     */
    if (KiArm64DebugRegistersEnabled(&ProcessorState->SpecialRegisters,
                                     NumBps,
                                     NumWps))
    {
        Mdscr |= ARM64_MDSCR_MDE | ARM64_MDSCR_KDE;
    }

    if (ProcessorState->ContextFrame.Cpsr & ARM64_PSTATE_SS)
        Mdscr |= ARM64_MDSCR_SS | ARM64_MDSCR_KDE;

    WRITE_SYSREG64(MDSCR_EL1, Mdscr);
    /* Synchronize the new debug state before returning from the exception. */
    ARM64_ISB();

    WRITE_SYSREG64(TCR_EL1, ProcessorState->ArchState.Tcr_El1);
    WRITE_SYSREG64(TTBR0_EL1, ProcessorState->ArchState.Ttbr0_El1);
    WRITE_SYSREG64(TTBR1_EL1, ProcessorState->ArchState.Ttbr1_El1);
    WRITE_SYSREG64(MAIR_EL1, ProcessorState->ArchState.Mair_El1);
    WRITE_SYSREG64(VBAR_EL1, ProcessorState->ArchState.Vbar_El1);
    WRITE_SYSREG64(SCTLR_EL1, ProcessorState->ArchState.Sctlr_El1);
    WRITE_SYSREG64(ACTLR_EL1, ProcessorState->ArchState.Actlr_El1);
    WRITE_SYSREG64(CPACR_EL1, ProcessorState->ArchState.Cpacr_El1);
    if (KiArm64PmuImplemented())
    {
        WRITE_SYSREG64(PMCR_EL0, ProcessorState->ArchState.Pmcr_El0);
        WRITE_SYSREG64(PMCNTENSET_EL0, ProcessorState->ArchState.Pmcntenset_El0);
        WRITE_SYSREG64(PMUSERENR_EL0, ProcessorState->ArchState.Pmuserenr_El0);
    }
}

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    /*
     * ARM64 cache maintenance for coherency.
     *
     * Unlike x86/AMD64 which has hardware cache coherency, ARM64 requires
     * explicit cache maintenance operations. When mapping physical RAM
     * (e.g., ramdisk) with cached attributes, we must ensure data cache
     * coherency to prevent stale cache lines from being read.
     *
     * Sequence:
     * 1. DC CIVAC (Data Cache Clean and Invalidate by VA to PoC) - Not
     *    practical here as we don't have specific VAs, so we use set/way ops
     * 2. IC IALLU (Instruction Cache Invalidate All to PoU)
     * 3. DSB + ISB to ensure ordering
     *
     * For set/way operations, we perform a full D-cache clean and invalidate
     * operation to ensure all dirty data is written back and stale lines removed.
     * This is necessary when establishing new mappings to RAM (MmMapIoSpace with
     * MmCached) to prevent the cache manager from reading stale data.
     */
    ARM64_DSB(ISH);

    KiArm64EnsureDcacheSweepGeometry();

    for (ULONG LevelIndex = 0; LevelIndex < KiArm64DcacheSweepLevelCount; LevelIndex++)
    {
        PARM64_CACHE_SWEEP_LEVEL Geometry = &KiArm64DcacheSweepLevels[LevelIndex];

        for (ULONG WayLoop = 0; WayLoop < Geometry->NumWays; WayLoop++)
        {
            for (ULONG SetLoop = 0; SetLoop < Geometry->NumSets; SetLoop++)
            {
                ULONG64 SetWayValue = (((ULONG64)Geometry->Level) << 1) |
                                      (((ULONG64)SetLoop) << Geometry->SetShift) |
                                      (((ULONG64)WayLoop) << Geometry->WayShift);
                ARM64_DC(CISW, SetWayValue);
            }
        }
    }

    ARM64_DSB(ISH);

    /* Invalidate instruction cache */
    ARM64_IC_ALL(IALLU);
    ARM64_DSB(ISH);
    ARM64_ISB();

    return TRUE;
}

VOID
FASTCALL
KeInvalidateRangeAllCaches(
    _In_ PVOID BaseAddress,
    _In_ ULONG Length)
{
    ULONG_PTR Address, EndAddress;
    ULONG CacheLineSize;

    if (!Length)
        return;

    CacheLineSize = KeLargestCacheLine ? KeLargestCacheLine : 64;
    Address = (ULONG_PTR)BaseAddress & ~((ULONG_PTR)CacheLineSize - 1);
    EndAddress = (ULONG_PTR)BaseAddress + Length;

    ARM64_DSB(SY);
    do
    {
        ARM64_DC(CIVAC, Address);
        ARM64_IC(IVAU, Address);
        Address += CacheLineSize;
    } while ((Address < EndAddress) && (Address != 0));
    ARM64_DSB(SY);
    ARM64_ISB();
}

ULONG
NTAPI
KeGetRecommendedSharedDataAlignment(VOID)
{
    return max(KeLargestCacheLine, SYSTEM_CACHE_ALIGNMENT_SIZE);
}

VOID
NTAPI
KeFlushEntireTb(_In_ BOOLEAN Invalid,
                _In_ BOOLEAN AllProcessors)
{
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Invalid);

    /* Serialize with other TLB modifications. */
    OldIrql = KeRaiseIrqlToSynchLevel();

    ARM64_DSB(ISH);
    if (AllProcessors)
        ARM64_TLBI_ALL(VMALLE1IS);
    else
        ARM64_TLBI_ALL(VMALLE1);
    ARM64_DSB(ISH);
    ARM64_ISB();

    KeLowerIrql(OldIrql);
}

VOID
NTAPI
KeSetDmaIoCoherency(_In_ ULONG Coherency)
{
    KiDmaIoCoherency = Coherency;
}

VOID
__cdecl
KeSaveStateForHibernate(_In_ PKPROCESSOR_STATE State)
{
    UNREFERENCED_PARAMETER(State);
    ARM64_STUB();
}

VOID
NTAPI
KiSaveProcessorState(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb;
    PCONTEXT Context;

    Prcb = KeGetCurrentPrcb();
    if ((Prcb == NULL) || (TrapFrame == NULL))
    {
        return;
    }

    Context = &Prcb->ProcessorState.ContextFrame;
    Context->ContextFlags = CONTEXT_FULL |
                             CONTEXT_FLOATING_POINT |
                             CONTEXT_DEBUG_REGISTERS |
                             CONTEXT_ARM64 |
                             CONTEXT_X18;

    KeTrapFrameToContext(TrapFrame, ExceptionFrame, Context);
    KiSaveProcessorControlState(&Prcb->ProcessorState);
}

VOID
NTAPI
KiRestoreProcessorState(
    _Out_ PKTRAP_FRAME TrapFrame,
    _Out_ PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb;
    KPROCESSOR_MODE PreviousMode;

    Prcb = KeGetCurrentPrcb();
    if ((Prcb == NULL) || (TrapFrame == NULL))
    {
        return;
    }

    PreviousMode = (TrapFrame->PreviousMode == UserMode) ? UserMode : KernelMode;

    KeContextToTrapFrame(&Prcb->ProcessorState.ContextFrame,
                         ExceptionFrame,
                         TrapFrame,
                         CONTEXT_FULL |
                         CONTEXT_FLOATING_POINT |
                         CONTEXT_DEBUG_REGISTERS |
                         CONTEXT_ARM64 |
                         CONTEXT_X18,
                         PreviousMode);

    KiRestoreProcessorControlState(&Prcb->ProcessorState);
}

/*
 * @implemented
 * KeFlushIoBuffers - Flush CPU data caches for DMA correctness on ARM64.
 *
 * ARM64 uses a weakly-ordered memory model and does NOT have cache-coherent DMA
 * by default (unlike x86/AMD64). This means that when a DMA device reads or writes
 * memory, the CPU caches may contain stale data or unflushed writes.
 *
 * For DMA correctness, we must perform explicit cache maintenance:
 *
 * - ReadOperation == TRUE (device-to-memory DMA, e.g., disk read):
 *   The device will write data directly to RAM. We must INVALIDATE the CPU's
 *   data cache lines covering this memory region so subsequent CPU reads fetch
 *   the fresh data from RAM rather than stale cached data. DMA uses DC IVAC
 *   (Invalidate by VA to Point of Coherency), matching Windows ARM64. Partial
 *   boundary lines use DC CIVAC so bytes outside the MDL are not discarded.
 *   PIO reads use DC CIVAC because the CPU produced the data being published.
 *
 * - ReadOperation == FALSE (memory-to-device DMA, e.g., disk write):
 *   The CPU has written data that the device will read. We must CLEAN (flush)
 *   the CPU's data cache to ensure all modified cache lines are written back
 *   to RAM so the device sees the correct data. We use DC CVAC (Clean by VA
 *   to Point of Coherency).
 *
 * The DmaOperation parameter indicates whether this is for an actual DMA transfer.
 * If KiDmaIoCoherency is set, the hardware provides DMA coherency and we can
 * skip cache maintenance for DMA operations.
 *
 * Cache line alignment is critical: ARM64 cache maintenance operates on cache
 * lines (typically 64 bytes). We must align to cache line boundaries to avoid
 * corrupting adjacent data.
 *
 * Full-system barriers ensure cache operations reach the Point of Coherency
 * before an external DMA agent is allowed to observe or update the buffer.
 */
VOID
NTAPI
KeFlushIoBuffers(_Inout_ PMDL Mdl,
                 _In_ BOOLEAN ReadOperation,
                 _In_ BOOLEAN DmaOperation)
{
    PVOID VirtualAddress;
    PVOID OriginalAddress;
    ULONG Length;
    ULONG_PTR StartAddress;
    ULONG_PTR EndAddress;
    ULONG_PTR CacheAddress;
    ULONG CacheLineSize;
    BOOLEAN MappedByUs = FALSE;
    BOOLEAN UsePfnAlias = FALSE;
    PPFN_NUMBER MdlPages = NULL;
    ULONG PageCount = 0;
    ULONG ByteOffset;
    ULONG RemainingLength;
    ULONG PageIndex;

    /*
     * If the MDL is NULL or has zero byte count, there is nothing to flush.
     */
    if (Mdl == NULL || Mdl->ByteCount == 0)
    {
        return;
    }

    /*
     * For DMA operations, check if the hardware provides DMA coherency.
     * If KiDmaIoCoherency is non-zero, the system has cache-coherent DMA
     * and explicit cache maintenance is not needed.
     */
    if (DmaOperation && KiDmaIoCoherency)
    {
        return;
    }

    OriginalAddress = MmGetMdlVirtualAddress(Mdl);
    Length = Mdl->ByteCount;
    ByteOffset = MmGetMdlByteOffset(Mdl);

    CacheLineSize = KeLargestCacheLine;
    if (CacheLineSize == 0)
    {
        CacheLineSize = 64;  /* Default ARM64 cache line size */
    }

    /*
     * Prefer the stable PFN/KSEG0 alias for RAM-backed MDLs. This avoids cache
     * maintenance through transient user or pageable kernel virtual aliases,
     * which has already produced deferred SError failures on ARM64 bring-up.
     */
    if (((Mdl->MdlFlags & MDL_IO_SPACE) == 0) && (OriginalAddress != NULL))
    {
        MdlPages = MmGetMdlPfnArray(Mdl);
        PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(OriginalAddress, Length);
        UsePfnAlias = (MdlPages != NULL) && (PageCount != 0);
    }

    if (UsePfnAlias)
    {
        if (ReadOperation)
        {
            ARM64_DSB(SY);
        }

        RemainingLength = Length;
        for (PageIndex = 0; PageIndex < PageCount; PageIndex++)
        {
            ULONG ChunkLength;
            ULONG_PTR AliasVa;
            ULONG_PTR ChunkStart;
            ULONG_PTR ChunkEnd;

            ChunkLength = min(RemainingLength, (ULONG)(PAGE_SIZE - ByteOffset));
            AliasVa = MI_ARM64_PFN_TO_VA(MdlPages[PageIndex]);
            ChunkStart = AliasVa + ByteOffset;
            ChunkEnd = ChunkStart + ChunkLength;
            StartAddress = ChunkStart & ~((ULONG_PTR)CacheLineSize - 1);
            EndAddress = (ChunkEnd + CacheLineSize - 1) &
                         ~((ULONG_PTR)CacheLineSize - 1);

            for (CacheAddress = StartAddress;
                 CacheAddress < EndAddress;
                 CacheAddress += CacheLineSize)
            {
                if (ReadOperation)
                {
                    if (DmaOperation &&
                        CacheAddress >= ChunkStart &&
                        CacheAddress + CacheLineSize <= ChunkEnd)
                    {
                        ARM64_DC(IVAC, CacheAddress);
                    }
                    else
                    {
                        ARM64_DC(CIVAC, CacheAddress);
                    }
                }
                else
                {
                    ARM64_DC(CVAC, CacheAddress);
                }
            }

            RemainingLength -= ChunkLength;
            ByteOffset = 0;
        }

        ARM64_DSB(SY);
        return;
    }

    /*
     * Fall back to the original system-VA path for MDL_IO_SPACE or other
     * mappings that do not carry a normal RAM PFN array.
     */
    if (Mdl->MdlFlags & (MDL_MAPPED_TO_SYSTEM_VA | MDL_SOURCE_IS_NONPAGED_POOL))
    {
        VirtualAddress = Mdl->MappedSystemVa;
    }
    else
    {
        VirtualAddress = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (VirtualAddress == NULL)
        {
            DPRINT1("KeFlushIoBuffers: MDL %p not mapped and map failed, skipping cache maintenance (ReadOperation=%d DmaOperation=%d)\n",
                    Mdl, ReadOperation, DmaOperation);
            return;
        }
        MappedByUs = TRUE;
    }

    if (VirtualAddress == NULL)
    {
        return;
    }

    StartAddress = (ULONG_PTR)VirtualAddress & ~((ULONG_PTR)CacheLineSize - 1);
    EndAddress = ((ULONG_PTR)VirtualAddress + Length + CacheLineSize - 1) &
                 ~((ULONG_PTR)CacheLineSize - 1);

    if (ReadOperation)
    {
        ARM64_DSB(SY);

        for (CacheAddress = StartAddress; CacheAddress < EndAddress;
             CacheAddress += CacheLineSize)
        {
            if (DmaOperation &&
                CacheAddress >= (ULONG_PTR)VirtualAddress &&
                CacheAddress + CacheLineSize <=
                    (ULONG_PTR)VirtualAddress + Length)
            {
                ARM64_DC(IVAC, CacheAddress);
            }
            else
            {
                ARM64_DC(CIVAC, CacheAddress);
            }
        }

        ARM64_DSB(SY);
    }
    else
    {
        for (CacheAddress = StartAddress; CacheAddress < EndAddress;
             CacheAddress += CacheLineSize)
        {
            ARM64_DC(CVAC, CacheAddress);
        }

        ARM64_DSB(SY);
    }

    /*
     * If we mapped the MDL ourselves, unmap it now to leave the MDL in the
     * same state we found it. This prevents leaving the MDL mapped when the
     * caller expects it to be unmapped (e.g., before IoFreeMdl).
     */
    if (MappedByUs && (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA))
    {
        MmUnmapLockedPages(VirtualAddress, Mdl);
    }
}

/*
 * Windows ARM64 treats the base FP/NEON register bank as part of the normal
 * kernel ABI. The WDK exposes KFLOATING_SAVE as an opaque dummy and inlines
 * these operations as successful no-ops. Keep real exports for precompiled
 * drivers that import the routines instead of using the inline definitions.
 */
NTSTATUS
NTAPI
KxSaveFloatingPointState(
    _Out_ PKFLOATING_SAVE FloatingState)
{
    UNREFERENCED_PARAMETER(FloatingState);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KxRestoreFloatingPointState(
    _In_ PKFLOATING_SAVE FloatingState)
{
    UNREFERENCED_PARAMETER(FloatingState);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtVdmControl(_In_ ULONG ControlCode,
             _Inout_ PVOID ControlData)
{
    UNREFERENCED_PARAMETER(ControlCode);
    UNREFERENCED_PARAMETER(ControlData);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
NtSetLdtEntries(_In_ ULONG Selector1,
                _In_ LDT_ENTRY LdtEntry1,
                _In_ ULONG Selector2,
                _In_ LDT_ENTRY LdtEntry2)
{
    UNREFERENCED_PARAMETER(Selector1);
    UNREFERENCED_PARAMETER(LdtEntry1);
    UNREFERENCED_PARAMETER(Selector2);
    UNREFERENCED_PARAMETER(LdtEntry2);
    return STATUS_NOT_IMPLEMENTED;
}

/* Register state for the bugcheck screen and log. */
VOID
KiDisplayBugCheckRegisterState(
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context)
{
    CHAR Line[128];
    const ULONG64 *Registers;
    ULONG RegisterCount;
    ULONG Base;
    ULONG64 Pc;
    ULONG64 Lr;
    ULONG64 Sp;
    ULONG64 Fp;
    ULONG Cpsr;

    if (TrapFrame != NULL)
    {
        Registers = TrapFrame->X;
        RegisterCount = RTL_NUMBER_OF(TrapFrame->X);
        Pc = TrapFrame->Pc;
        Lr = TrapFrame->Lr;
        Sp = TrapFrame->Sp;
        Fp = TrapFrame->Fp;
        Cpsr = TrapFrame->Spsr;
    }
    else
    {
        Registers = Context->X;
        RegisterCount = 29;
        Pc = Context->Pc;
        Lr = Context->Lr;
        Sp = Context->Sp;
        Fp = Context->Fp;
        Cpsr = Context->Cpsr;
    }

    RtlStringCbPrintfA(Line, sizeof(Line), "PC=%016I64x LR=%016I64x SP=%016I64x\r\n", Pc, Lr, Sp);
    KiDisplayAndLogBugCheckString(Line);
    if (TrapFrame != NULL)
        RtlStringCbPrintfA(Line, sizeof(Line), "FP=%016I64x PSR=%08lx ESR=%08lx\r\n", Fp, Cpsr, TrapFrame->Esr);
    else
        RtlStringCbPrintfA(Line, sizeof(Line), "FP=%016I64x PSR=%08lx\r\n", Fp, Cpsr);
    KiDisplayAndLogBugCheckString(Line);

    for (Base = 0; Base < RegisterCount; Base += 3)
    {
        if ((RegisterCount - Base) >= 3)
            RtlStringCbPrintfA(Line, sizeof(Line), "X%02lu=%016I64x X%02lu=%016I64x X%02lu=%016I64x\r\n", Base, Registers[Base], Base + 1, Registers[Base + 1], Base + 2, Registers[Base + 2]);
        else if ((RegisterCount - Base) == 2)
            RtlStringCbPrintfA(Line, sizeof(Line), "X%02lu=%016I64x X%02lu=%016I64x\r\n", Base, Registers[Base], Base + 1, Registers[Base + 1]);
        else
            RtlStringCbPrintfA(Line, sizeof(Line), "X%02lu=%016I64x\r\n", Base, Registers[Base]);
        KiDisplayAndLogBugCheckString(Line);
    }
}
