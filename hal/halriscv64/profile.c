/*
 * PROJECT:     LiberNT RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hardware event profile sources through the SBI PMU extension
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include "halp.h"

#define RISCV_SBI_EXTENSION_PMU            0x504D55UL
#define RISCV_SBI_PMU_NUM_COUNTERS         0UL
#define RISCV_SBI_PMU_COUNTER_CONFIG       2UL
#define RISCV_SBI_PMU_COUNTER_START        3UL
#define RISCV_SBI_PMU_COUNTER_STOP         4UL
#define RISCV_SBI_PMU_CFG_CLEAR_VALUE      (1UL << 1)
#define RISCV_SBI_PMU_CFG_SET_MINH         (1UL << 7)
#define RISCV_SBI_PMU_START_SET_INIT_VALUE (1UL << 0)
#define RISCV_SBI_PMU_STOP_RESET           (1UL << 0)
#define RISCV_PMU_FIRST_HPM_COUNTER        3UL
#define RISCV_PMU_NO_COUNTER               MAXULONG

#define RISCV_PMU_DEFAULT_INTERVAL         65536UL
#define RISCV_PMU_MINIMUM_INTERVAL         1024UL

typedef struct _HAL_RISCV_PMU_SOURCE
{
    KPROFILE_SOURCE Source;
    ULONG EventIndex;
} HAL_RISCV_PMU_SOURCE;

static const HAL_RISCV_PMU_SOURCE HalpRiscvPmuSources[] =
{
    { ProfileTotalIssues,          0x00002 },
    { ProfileBranchInstructions,   0x00005 },
    { ProfileDcacheMisses,         0x10001 },
    { ProfileIcacheMisses,         0x10009 },
    { ProfileCacheMisses,          0x00004 },
    { ProfileBranchMispredictions, 0x00006 },
    { ProfileTotalCycles,          0x00001 },
    { ProfileDcacheAccesses,       0x10000 },
};

#define RISCV_PMU_SOURCES RTL_NUMBER_OF(HalpRiscvPmuSources)

VOID NTAPI KeProfileInterruptWithSource(PKTRAP_FRAME TrapFrame, KPROFILE_SOURCE Source);

typedef struct _HAL_RISCV_PMU_HART
{
    ULONG Generation;
    ULONG Counter[RISCV_PMU_SOURCES];
} HAL_RISCV_PMU_HART;

static HAL_RISCV_PMU_HART HalpRiscvPmuHarts[MAXIMUM_PROCESSORS];
static ULONG HalpRiscvPmuCounterCount;
static ULONG HalpRiscvPmuSupported;
static volatile ULONG HalpRiscvPmuEnabled;
static volatile ULONG HalpRiscvPmuGeneration;
static ULONG HalpRiscvPmuInterval[RISCV_PMU_SOURCES];
static pHalQuerySystemInformation HalpRiscvDefaultQuerySystemInformation;
static pHalSetSystemInformation HalpRiscvDefaultSetSystemInformation;

static
ULONG
HalpRiscvPmuFindSource(
    _In_ KPROFILE_SOURCE Source)
{
    ULONG Index;

    for (Index = 0; Index < RISCV_PMU_SOURCES; Index++)
    {
        if (HalpRiscvPmuSources[Index].Source == Source)
            return Index;
    }
    return RISCV_PMU_NO_COUNTER;
}

static
ULONG_PTR
HalpRiscvPmuCounterMask(VOID)
{
    ULONG Count = HalpRiscvPmuCounterCount - RISCV_PMU_FIRST_HPM_COUNTER;

    return (Count >= sizeof(ULONG_PTR) * 8) ? ~(ULONG_PTR)0 : (((ULONG_PTR)1 << Count) - 1);
}

static
ULONG
HalpRiscvPmuConfigure(
    _In_ ULONG Index)
{
    RISCV_SBI_RETURN Result;

    Result = HalpRiscvSbiCall(RISCV_SBI_EXTENSION_PMU,
                              RISCV_SBI_PMU_COUNTER_CONFIG,
                              RISCV_PMU_FIRST_HPM_COUNTER,
                              HalpRiscvPmuCounterMask(),
                              RISCV_SBI_PMU_CFG_CLEAR_VALUE | RISCV_SBI_PMU_CFG_SET_MINH,
                              HalpRiscvPmuSources[Index].EventIndex);
    if ((Result.Error != 0) || (Result.Value < RISCV_PMU_FIRST_HPM_COUNTER) ||
        (Result.Value >= HalpRiscvPmuCounterCount))
    {
        return RISCV_PMU_NO_COUNTER;
    }
    return (ULONG)Result.Value;
}

static
VOID
HalpRiscvPmuRelease(
    _In_ ULONG Counter)
{
    HalpRiscvSbiCall(RISCV_SBI_EXTENSION_PMU, RISCV_SBI_PMU_COUNTER_STOP,
                     Counter, 1, RISCV_SBI_PMU_STOP_RESET, 0);
}

static
VOID
HalpRiscvPmuArm(
    _In_ ULONG Counter,
    _In_ ULONG Interval)
{
    HalpRiscvSbiCall(RISCV_SBI_EXTENSION_PMU, RISCV_SBI_PMU_COUNTER_START,
                     Counter, 1, RISCV_SBI_PMU_START_SET_INIT_VALUE,
                     (ULONG_PTR)0 - Interval);
}

VOID
HalpRiscvInitializeProfile(VOID)
{
    RISCV_SBI_RETURN Result;
    ULONG Index, Counter, Processor;

    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        for (Index = 0; Index < RISCV_PMU_SOURCES; Index++)
            HalpRiscvPmuHarts[Processor].Counter[Index] = RISCV_PMU_NO_COUNTER;
    }
    for (Index = 0; Index < RISCV_PMU_SOURCES; Index++)
        HalpRiscvPmuInterval[Index] = RISCV_PMU_DEFAULT_INTERVAL;

    if (!(KiRiscvQueryFeatureFlags() & RISCV_HAL_FEATURE_SSCOFPMF) ||
        !HalpRiscvSbiExtensionAvailable(RISCV_SBI_EXTENSION_PMU))
    {
        return;
    }

    Result = HalpRiscvSbiCall(RISCV_SBI_EXTENSION_PMU, RISCV_SBI_PMU_NUM_COUNTERS, 0, 0, 0, 0);
    if ((Result.Error != 0) || (Result.Value <= RISCV_PMU_FIRST_HPM_COUNTER))
        return;
    HalpRiscvPmuCounterCount = (ULONG)min(Result.Value, 64);

    for (Index = 0; Index < RISCV_PMU_SOURCES; Index++)
    {
        Counter = HalpRiscvPmuConfigure(Index);
        if (Counter == RISCV_PMU_NO_COUNTER)
            continue;
        HalpRiscvPmuRelease(Counter);
        HalpRiscvPmuSupported |= 1UL << Index;
    }
    DbgPrint("RISC-V PMU: %lu counters, profile sources 0x%lx\n",
             HalpRiscvPmuCounterCount, HalpRiscvPmuSupported);
}

VOID
HalpRiscvEnableProfileInterrupt(VOID)
{
    if (HalpRiscvPmuSupported)
        KiRiscvSetInterruptEnabled(RISCV_HAL_SIE_LCOFIE, TRUE);
}

VOID
HalpRiscvSyncProfileCounters(VOID)
{
    HAL_RISCV_PMU_HART *Hart = &HalpRiscvPmuHarts[KeGetCurrentProcessorNumber()];
    ULONG Generation = HalpRiscvPmuGeneration;
    ULONG Enabled = HalpRiscvPmuEnabled;
    ULONG Index;

    if (Hart->Generation == Generation)
        return;
    Hart->Generation = Generation;

    for (Index = 0; Index < RISCV_PMU_SOURCES; Index++)
    {
        BOOLEAN Wanted = (Enabled & (1UL << Index)) != 0;

        if (!Wanted && (Hart->Counter[Index] != RISCV_PMU_NO_COUNTER))
        {
            HalpRiscvPmuRelease(Hart->Counter[Index]);
            Hart->Counter[Index] = RISCV_PMU_NO_COUNTER;
        }
        else if (Wanted && (Hart->Counter[Index] == RISCV_PMU_NO_COUNTER))
        {
            Hart->Counter[Index] = HalpRiscvPmuConfigure(Index);
            if (Hart->Counter[Index] != RISCV_PMU_NO_COUNTER)
                HalpRiscvPmuArm(Hart->Counter[Index], HalpRiscvPmuInterval[Index]);
        }
    }
}

BOOLEAN
HalpRiscvStartPmuProfile(
    _In_ KPROFILE_SOURCE Source)
{
    ULONG Index = HalpRiscvPmuFindSource(Source);

    if ((Index == RISCV_PMU_NO_COUNTER) || !(HalpRiscvPmuSupported & (1UL << Index)))
        return FALSE;
    InterlockedOr((PLONG)&HalpRiscvPmuEnabled, 1L << Index);
    InterlockedIncrement((PLONG)&HalpRiscvPmuGeneration);
    HalpRiscvSyncProfileCounters();
    return TRUE;
}

BOOLEAN
HalpRiscvStopPmuProfile(
    _In_ KPROFILE_SOURCE Source)
{
    ULONG Index = HalpRiscvPmuFindSource(Source);

    if (Index == RISCV_PMU_NO_COUNTER)
        return FALSE;
    InterlockedAnd((PLONG)&HalpRiscvPmuEnabled, ~(1L << Index));
    InterlockedIncrement((PLONG)&HalpRiscvPmuGeneration);
    HalpRiscvSyncProfileCounters();
    return TRUE;
}

VOID
NTAPI
HalpRiscvProfileInterrupt(
    _In_ PKTRAP_FRAME TrapFrame)
{
    HAL_RISCV_PMU_HART *Hart = &HalpRiscvPmuHarts[KeGetCurrentProcessorNumber()];
    ULONG64 Overflow;
    ULONG Index, Counter;

    __asm__ __volatile__("csrr %0, 0xda0" : "=r"(Overflow));
    __asm__ __volatile__("csrc sip, %0" :: "r"(RISCV_HAL_SIE_LCOFIE) : "memory");

    for (Index = 0; Index < RISCV_PMU_SOURCES; Index++)
    {
        Counter = Hart->Counter[Index];
        if ((Counter == RISCV_PMU_NO_COUNTER) || !(Overflow & (1ULL << Counter)))
            continue;
        HalpRiscvSbiCall(RISCV_SBI_EXTENSION_PMU, RISCV_SBI_PMU_COUNTER_STOP, Counter, 1, 0, 0);
        if (HalpRiscvPmuEnabled & (1UL << Index))
            KeProfileInterruptWithSource(TrapFrame, HalpRiscvPmuSources[Index].Source);
        HalpRiscvPmuArm(Counter, HalpRiscvPmuInterval[Index]);
    }
}

static
NTSTATUS
NTAPI
HalpRiscvQuerySystemInformation(
    _In_ HAL_QUERY_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _Inout_ PVOID Buffer,
    _Out_ PULONG ReturnedLength)
{
    PHAL_PROFILE_SOURCE_INFORMATION Information = Buffer;
    PHAL_ACPI_ROOT_POINTER_INFORMATION AcpiInformation = Buffer;
    ULONG Index;

    if (ReturnedLength)
        *ReturnedLength = 0;

    switch (InformationClass)
    {
        case HalProfileSourceInformation:
            break;

        case HalAcpiAuditInformation:
            if (BufferSize < sizeof(*AcpiInformation))
                return STATUS_INFO_LENGTH_MISMATCH;
            AcpiInformation->RsdpPhysicalAddress.QuadPart = 0;
            if (ReturnedLength)
                *ReturnedLength = sizeof(*AcpiInformation);
            return STATUS_NOT_FOUND;

        case HalFrameBufferCachingInformation:
        case HalDisplayBiosInformation:
        case HalQueryAMLIIllegalIOPortAddresses:
            return STATUS_INVALID_LEVEL;

        default:
            return HalpRiscvDefaultQuerySystemInformation(InformationClass, BufferSize, Buffer, ReturnedLength);
    }

    if (BufferSize < sizeof(*Information))
        return STATUS_INFO_LENGTH_MISMATCH;

    Index = HalpRiscvPmuFindSource(Information->Source);
    Information->Supported = (Index != RISCV_PMU_NO_COUNTER) &&
                             ((HalpRiscvPmuSupported & (1UL << Index)) != 0);
    Information->Interval = Information->Supported ? HalpRiscvPmuInterval[Index] : 0;
    if (ReturnedLength)
        *ReturnedLength = sizeof(*Information);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HalpRiscvSetSystemInformation(
    _In_ HAL_SET_INFORMATION_CLASS InformationClass,
    _In_ ULONG BufferSize,
    _In_ PVOID Buffer)
{
    PHAL_PROFILE_SOURCE_INTERVAL Interval = Buffer;
    ULONG Index;

    if (InformationClass != HalProfileSourceInterval)
        return HalpRiscvDefaultSetSystemInformation(InformationClass, BufferSize, Buffer);
    if (BufferSize < sizeof(*Interval))
        return STATUS_INFO_LENGTH_MISMATCH;

    Index = HalpRiscvPmuFindSource(Interval->Source);
    if ((Index == RISCV_PMU_NO_COUNTER) || !(HalpRiscvPmuSupported & (1UL << Index)))
        return STATUS_NOT_IMPLEMENTED;
    HalpRiscvPmuInterval[Index] = (ULONG)min(max(Interval->Interval, RISCV_PMU_MINIMUM_INTERVAL), MAXULONG);
    return STATUS_SUCCESS;
}

VOID
HalpRiscvRegisterProfileInformation(VOID)
{
    HalpRiscvDefaultQuerySystemInformation = HalQuerySystemInformation;
    HalpRiscvDefaultSetSystemInformation = HalSetSystemInformation;
    HalQuerySystemInformation = HalpRiscvQuerySystemInformation;
    HalSetSystemInformation = HalpRiscvSetSystemInformation;
}
