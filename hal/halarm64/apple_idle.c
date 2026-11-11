/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon core power-down in WFI for processor idle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ioaccess.h>
#include <reactos/arm64/sysreg.h>
#include <pseh/pseh2.h>
#include "halext.h"
#include "devtree.h"
#include "apple_aic.h"

#define NDEBUG
#include <debug.h>

#define APPLE_IDLE_IMPLEMENTER(Midr)    (((Midr) >> 24) & 0xFF)
#define APPLE_IDLE_IMPLEMENTER_APPLE    0x61
#define APPLE_IDLE_AIDR_RETENTION       (1ULL << 2)
#define APPLE_IDLE_POWER_DOWN_MASK      (3ULL << 24)
#define APPLE_IDLE_FORCE_UP             (2ULL << 24)
#define APPLE_IDLE_FORCE_DOWN           (3ULL << 24)
#define APPLE_IDLE_RESIDENCY_US         10000
#define APPLE_IDLE_REPORT_SECONDS       60
#define APPLE_IDLE_PATTERN              0x5AA5C33C0FF0E11EULL
#define APPLE_IDLE_TIMER_ENABLE         0x1ULL
#define APPLE_IDLE_TIMER_IMASK          0x2ULL
#define APPLE_IDLE_TIMER_ARMED_MASK     (APPLE_IDLE_TIMER_ENABLE | APPLE_IDLE_TIMER_IMASK)
#define APPLE_IDLE_DAIF_IF              0xC0ULL
#define APPLE_IDLE_REGISTER_COUNT       17

#ifndef ARM64_SR_S3_5_C15_C5_0
#define ARM64_SR_S3_5_C15_C5_0          ARM64_SR_ENCODE(3, 5, 15, 5, 0)
#endif

typedef struct _APPLE_IDLE_WFI
{
    ULONG64 PowerDown;
    ULONG64 Resume;
    ULONG64 Pattern;
    ULONG64 PatternAfter;
    ULONG64 Exits;
} APPLE_IDLE_WFI, *PAPPLE_IDLE_WFI;

C_ASSERT(FIELD_OFFSET(APPLE_IDLE_WFI, PowerDown) == 0x00);
C_ASSERT(FIELD_OFFSET(APPLE_IDLE_WFI, Resume) == 0x08);
C_ASSERT(FIELD_OFFSET(APPLE_IDLE_WFI, Pattern) == 0x10);
C_ASSERT(FIELD_OFFSET(APPLE_IDLE_WFI, PatternAfter) == 0x18);
C_ASSERT(FIELD_OFFSET(APPLE_IDLE_WFI, Exits) == 0x20);

typedef struct DECLSPEC_ALIGN(128) _APPLE_IDLE_CPU
{
    BOOLEAN Ready;
    BOOLEAN Checked;
    ULONG64 ResidencyTicks;
    ULONG64 LastIdleTicks;
    ULONG64 ForceUp;
    ULONG64 ForceDown;
    ULONG64 IdleTicks;
    ULONG64 DownTicks;
    ULONG64 Entries;
    ULONG64 DownEntries;
    ULONG64 Exits;
} APPLE_IDLE_CPU, *PAPPLE_IDLE_CPU;

ULONG64
AppleIdleReadCycleOverride(VOID);

VOID
AppleIdleWriteCycleOverride(
    _In_ ULONG64 Value);

ULONG64
AppleIdleReadAuxiliaryId(VOID);

VOID
AppleIdlePowerDownWfi(
    _Inout_ PAPPLE_IDLE_WFI Wfi);

static const PCSTR AppleIdleRegisterNames[APPLE_IDLE_REGISTER_COUNT] =
{
    "SCTLR_EL1", "ACTLR_EL1", "CPACR_EL1", "TTBR0_EL1", "TTBR1_EL1", "TCR_EL1", "MAIR_EL1", "VBAR_EL1",
    "TPIDR_EL1", "TPIDR_EL0", "TPIDRRO_EL0", "CNTKCTL_EL1", "CNTV_CTL_EL0", "CNTV_CVAL_EL0", "CNTP_CTL_EL0",
    "CNTP_CVAL_EL0", "MDSCR_EL1",
};

static APPLE_IDLE_CPU AppleIdleCpu[MAXIMUM_PROCESSORS];
static volatile LONG AppleIdlePowerDownDisabled;
static volatile LONG64 AppleIdleReportDue;
static ULONG64 AppleIdleReportStart;
static ULONG64 AppleIdleReportIdle[MAXIMUM_PROCESSORS];
static ULONG64 AppleIdleReportDown[MAXIMUM_PROCESSORS];
static ULONG64 AppleIdleReportEntries[MAXIMUM_PROCESSORS];
static ULONG64 AppleIdleReportDownEntries[MAXIMUM_PROCESSORS];
static ULONG64 AppleIdleReportExits[MAXIMUM_PROCESSORS];

static VOID AppleIdleProcessor(VOID);

static const HAL_ARM64_PROCESSOR_IDLE AppleIdleOps =
{
    "Apple core power-down in WFI",
    AppleIdleProcessor,
};

static
VOID
AppleIdleReadRegisters(
    _Out_writes_(APPLE_IDLE_REGISTER_COUNT) PULONG64 Values)
{
    Values[0] = ARM64_READ_SYSREG(SCTLR_EL1);
    Values[1] = ARM64_READ_SYSREG(ACTLR_EL1);
    Values[2] = ARM64_READ_SYSREG(CPACR_EL1);
    Values[3] = ARM64_READ_SYSREG(TTBR0_EL1);
    Values[4] = ARM64_READ_SYSREG(TTBR1_EL1);
    Values[5] = ARM64_READ_SYSREG(TCR_EL1);
    Values[6] = ARM64_READ_SYSREG(MAIR_EL1);
    Values[7] = ARM64_READ_SYSREG(VBAR_EL1);
    Values[8] = ARM64_READ_SYSREG(TPIDR_EL1);
    Values[9] = ARM64_READ_SYSREG(TPIDR_EL0);
    Values[10] = ARM64_READ_SYSREG(TPIDRRO_EL0);
    Values[11] = ARM64_READ_SYSREG(CNTKCTL_EL1);
    Values[12] = ARM64_READ_SYSREG(CNTV_CTL_EL0) & APPLE_IDLE_TIMER_ARMED_MASK;
    Values[13] = ARM64_READ_SYSREG(CNTV_CVAL_EL0);
    Values[14] = ARM64_READ_SYSREG(CNTP_CTL_EL0) & APPLE_IDLE_TIMER_ARMED_MASK;
    Values[15] = ARM64_READ_SYSREG(CNTP_CVAL_EL0);
    Values[16] = ARM64_READ_SYSREG(MDSCR_EL1);
}

static
ULONG64
AppleIdleTimerBudget(VOID)
{
    ULONG64 Budget = MAXULONG64;
    ULONG64 Compare, Count;

    if ((ARM64_READ_SYSREG(CNTV_CTL_EL0) & APPLE_IDLE_TIMER_ARMED_MASK) == APPLE_IDLE_TIMER_ENABLE)
    {
        Compare = ARM64_READ_SYSREG(CNTV_CVAL_EL0);
        Count = ARM64_READ_SYSREG(CNTVCT_EL0);
        Budget = (Compare > Count) ? Compare - Count : 0;
    }
    if ((ARM64_READ_SYSREG(CNTP_CTL_EL0) & APPLE_IDLE_TIMER_ARMED_MASK) == APPLE_IDLE_TIMER_ENABLE)
    {
        Compare = ARM64_READ_SYSREG(CNTP_CVAL_EL0);
        Count = ARM64_READ_SYSREG(CNTPCT_EL0);
        Budget = min(Budget, (Compare > Count) ? Compare - Count : 0);
    }
    return Budget;
}

static
VOID
AppleIdleClockGate(VOID)
{
    ARM64_DSB(SY);
    ARM64_ISB();
    ARM64_WFI();
}

static
VOID
AppleIdleInitializeProcessor(
    _Inout_ PAPPLE_IDLE_CPU Cpu,
    _In_ ULONG Number)
{
    ULONG64 Control, Frequency;

    ASSERT(!Cpu->Ready);

    Control = AppleIdleReadCycleOverride();
    Frequency = ARM64_READ_SYSREG(CNTFRQ_EL0);
    Cpu->ResidencyTicks = Frequency ? (Frequency * APPLE_IDLE_RESIDENCY_US) / 1000000 : MAXULONG64;
    Cpu->LastIdleTicks = Cpu->ResidencyTicks;
    Cpu->ForceUp = (Control & ~APPLE_IDLE_POWER_DOWN_MASK) | APPLE_IDLE_FORCE_UP;
    Cpu->ForceDown = (Control & ~APPLE_IDLE_POWER_DOWN_MASK) | APPLE_IDLE_FORCE_DOWN;
    if (Control != Cpu->ForceUp)
    {
        AppleIdleWriteCycleOverride(Cpu->ForceUp);
        DPRINT1("Apple core idle: CPU %lu had CYC_OVRD %I64x, now %I64x so that a plain WFI stays clock-gated\n",
                Number, Control, AppleIdleReadCycleOverride());
    }
    ASSERT(Cpu->ResidencyTicks);
    Cpu->Ready = TRUE;
}

static
VOID
AppleIdleCheckProcessor(
    _Inout_ PAPPLE_IDLE_CPU Cpu,
    _In_ ULONG Number,
    _In_reads_(APPLE_IDLE_REGISTER_COUNT) const ULONG64 *Before,
    _In_reads_(APPLE_IDLE_REGISTER_COUNT) const ULONG64 *After,
    _In_ const APPLE_IDLE_WFI *Wfi,
    _In_ ULONG64 Ticks)
{
    ULONG64 Frequency = ARM64_READ_SYSREG(CNTFRQ_EL0);
    ULONG Index;

    ASSERT(!Cpu->Checked);
    Cpu->Checked = TRUE;
    for (Index = 0; Index < APPLE_IDLE_REGISTER_COUNT; ++Index)
    {
        if (Before[Index] != After[Index])
        {
            InterlockedExchange(&AppleIdlePowerDownDisabled, TRUE);
            DPRINT1("Apple core idle: CPU %lu lost %s across power-down in WFI (%I64x, then %I64x); power-down "
                    "disabled on every CPU\n",
                    Number, AppleIdleRegisterNames[Index], Before[Index], After[Index]);
            return;
        }
    }
    DPRINT1("Apple core idle: CPU %lu came back from power-down in WFI after %I64u us and %I64u WFI exits with its "
            "system registers intact, SIMD v16 %s, CYC_OVRD %I64x\n",
            Number, Frequency ? (Ticks * 1000000) / Frequency : 0, Wfi->Exits,
            (Wfi->PatternAfter == Wfi->Pattern) ? "kept" : "clobbered", AppleIdleReadCycleOverride());
}

static
VOID
AppleIdleReport(
    _In_ ULONG64 Now)
{
    ULONG64 Due = (ULONG64)AppleIdleReportDue;
    ULONG64 Frequency, Wall, Idle, Down, Entries, DownEntries, Exits, CpuIdle, CpuDown, LeastShare, Share;
    ULONG Number, Cpus = 0, Least = 0;

    if (!Due || Now < Due)
        return;
    Frequency = ARM64_READ_SYSREG(CNTFRQ_EL0);
    if ((ULONG64)InterlockedCompareExchange64(&AppleIdleReportDue, (LONG64)(Now + Frequency * APPLE_IDLE_REPORT_SECONDS),
                                              (LONG64)Due) != Due)
    {
        return;
    }

    Wall = Now - AppleIdleReportStart;
    Idle = Down = Entries = DownEntries = Exits = 0;
    LeastShare = 1000;
    for (Number = 0; Number < MAXIMUM_PROCESSORS; ++Number)
    {
        if (!AppleIdleCpu[Number].Ready)
            continue;
        CpuIdle = AppleIdleCpu[Number].IdleTicks - AppleIdleReportIdle[Number];
        CpuDown = AppleIdleCpu[Number].DownTicks - AppleIdleReportDown[Number];
        Idle += CpuIdle;
        Down += CpuDown;
        Entries += AppleIdleCpu[Number].Entries - AppleIdleReportEntries[Number];
        DownEntries += AppleIdleCpu[Number].DownEntries - AppleIdleReportDownEntries[Number];
        Exits += AppleIdleCpu[Number].Exits - AppleIdleReportExits[Number];
        Share = CpuIdle ? (CpuDown * 1000) / CpuIdle : 0;
        if (Share < LeastShare)
        {
            LeastShare = Share;
            Least = Number;
        }
        AppleIdleReportIdle[Number] = AppleIdleCpu[Number].IdleTicks;
        AppleIdleReportDown[Number] = AppleIdleCpu[Number].DownTicks;
        AppleIdleReportEntries[Number] = AppleIdleCpu[Number].Entries;
        AppleIdleReportDownEntries[Number] = AppleIdleCpu[Number].DownEntries;
        AppleIdleReportExits[Number] = AppleIdleCpu[Number].Exits;
        Cpus++;
    }
    AppleIdleReportStart = Now;
    KeMemoryBarrier();
    if (!Cpus || !Wall)
        return;

    DPRINT1("Apple core idle: last %I64u s on %lu CPUs: idle %I64u%% of the time, powered down %I64u%% of the idle "
            "time (least CPU %lu at %I64u%%), %I64u power-downs with %I64u WFI exits, %I64u clock-gated WFIs%s\n",
            Frequency ? Wall / Frequency : 0, Cpus, (Idle * 100) / (Wall * Cpus), Idle ? (Down * 100) / Idle : 0,
            Least, LeastShare / 10, DownEntries, Exits, Entries - DownEntries,
            AppleIdlePowerDownDisabled ? ", power-down disabled" : "");
}

static
VOID
AppleIdleProcessor(VOID)
{
    ULONG64 Before[APPLE_IDLE_REGISTER_COUNT], After[APPLE_IDLE_REGISTER_COUNT];
    APPLE_IDLE_WFI Wfi;
    PAPPLE_IDLE_CPU Cpu;
    ULONG64 Start, End, Budget;
    ULONG Number;
    BOOLEAN PowerDown;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT((ARM64_READ_SYSREG(DAIF) & APPLE_IDLE_DAIF_IF) == APPLE_IDLE_DAIF_IF);

    Number = KeGetCurrentProcessorNumber();
    if (Number >= MAXIMUM_PROCESSORS)
    {
        AppleIdleClockGate();
        return;
    }
    Cpu = &AppleIdleCpu[Number];
    if (!Cpu->Ready)
        AppleIdleInitializeProcessor(Cpu, Number);

    Start = ARM64_READ_SYSREG(CNTVCT_EL0);
    Budget = AppleIdleTimerBudget();
    PowerDown = !AppleIdlePowerDownDisabled && Budget >= Cpu->ResidencyTicks &&
                Cpu->LastIdleTicks >= Cpu->ResidencyTicks;
    if (PowerDown)
    {
        Wfi.PowerDown = Cpu->ForceDown;
        Wfi.Resume = Cpu->ForceUp;
        Wfi.Pattern = APPLE_IDLE_PATTERN ^ Number;
        Wfi.PatternAfter = 0;
        Wfi.Exits = 0;
        if (!Cpu->Checked)
            AppleIdleReadRegisters(Before);
        AppleIdlePowerDownWfi(&Wfi);
        End = ARM64_READ_SYSREG(CNTVCT_EL0);
        ASSERT(Wfi.Exits);
        if (!Cpu->Checked)
        {
            AppleIdleReadRegisters(After);
            AppleIdleCheckProcessor(Cpu, Number, Before, After, &Wfi, End - Start);
        }
        Cpu->DownTicks += End - Start;
        Cpu->DownEntries++;
        Cpu->Exits += Wfi.Exits;
    }
    else
    {
        AppleIdleClockGate();
        End = ARM64_READ_SYSREG(CNTVCT_EL0);
    }
    ASSERT(End >= Start);
    Cpu->LastIdleTicks = End - Start;
    Cpu->IdleTicks += End - Start;
    Cpu->Entries++;
    AppleIdleReport(End);
}

static
BOOLEAN
AppleIdleProbeCycleOverride(
    _Out_ PULONG64 Control)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG64 Value = 0;

    _SEH2_TRY
    {
        Value = ARM64_READ_SYSREG(S3_5_C15_C5_0);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    *Control = Value;
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Apple core idle: CYC_OVRD is not accessible at EL1 (exception %lx on CPU %lu), cores idle in plain "
                "WFI\n", Status, KeGetCurrentProcessorNumber());
        return FALSE;
    }
    return TRUE;
}

VOID
AppleIdleProbe(VOID)
{
    static const PCSTR Socs[] =
    {
        "apple,t8103", "apple,t8112", "apple,t8122", "apple,t6000", "apple,t6001", "apple,t6002", "apple,t6020",
        "apple,t6021", "apple,t6022", "apple,t6030", "apple,t6031", "apple,t6032", "apple,t6034",
    };
    const VOID *Blob;
    const VOID *Compatible;
    ULONG64 Midr, Aidr, Control, Now;
    ULONG Size, Root, Length, Index;
    NTSTATUS Status;
    FDT Fdt;

    if (!AppleAicDetected)
        return;
    Midr = ARM64_READ_SYSREG(MIDR_EL1);
    if (APPLE_IDLE_IMPLEMENTER(Midr) != APPLE_IDLE_IMPLEMENTER_APPLE)
    {
        DPRINT1("Apple core idle: MIDR_EL1 %I64x is not an Apple core; idle stays a clock-gated WFI\n", Midr);
        return;
    }
    if (!HalpArm64GetDeviceTree(&Blob, &Size) || !FdtOpen(Blob, Size, &Fdt) ||
        (Root = FdtRootNode(&Fdt)) == FDT_NO_NODE)
    {
        DPRINT1("Apple core idle: no device tree; idle stays a clock-gated WFI\n");
        return;
    }
    Compatible = FdtGetProperty(&Fdt, Root, "compatible", &Length);
    for (Index = 0; Index < RTL_NUMBER_OF(Socs); ++Index)
    {
        if (FdtStringListContains(Compatible, Length, Socs[Index]))
            break;
    }
    if (!FdtStringListContains(Compatible, Length, "apple,arm-platform") || Index == RTL_NUMBER_OF(Socs))
    {
        DPRINT1("Apple core idle: this SoC has no known core power-down in WFI; idle stays a clock-gated WFI\n");
        return;
    }
    Aidr = AppleIdleReadAuxiliaryId();
    if (!(Aidr & APPLE_IDLE_AIDR_RETENTION))
    {
        DPRINT1("Apple core idle: AIDR_EL1 %I64x has no state retention; idle stays a clock-gated WFI\n", Aidr);
        return;
    }
    DPRINT1("Apple core idle: reading CYC_OVRD on %s\n", Socs[Index]);
    if (!AppleIdleProbeCycleOverride(&Control))
        return;

    Now = ARM64_READ_SYSREG(CNTVCT_EL0);
    AppleIdleReportStart = Now;
    InterlockedExchange64(&AppleIdleReportDue, (LONG64)(Now + ARM64_READ_SYSREG(CNTFRQ_EL0) * APPLE_IDLE_REPORT_SECONDS));
    Status = HalpArm64RegisterProcessorIdle(&AppleIdleOps);
    DPRINT1("Apple core idle: %s, MIDR_EL1 %I64x, AIDR_EL1 %I64x, CYC_OVRD %I64x; cores power down in WFI when the "
            "next timer is %lu us away and their last idle lasted as long: %lx\n",
            Socs[Index], Midr, Aidr, Control, APPLE_IDLE_RESIDENCY_US, Status);
}
