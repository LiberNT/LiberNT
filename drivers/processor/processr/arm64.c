/*
 * PROJECT:     LiberNT Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 processor identity
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "processr.h"
#include <reactos/arm64/acpi.h>

#define PROC_MPIDR_AFFINITY     0xFF00FFFFFFULL

VOID
ProcArchQueryFeatures(
    _Out_ PPROC_FEATURES Features)
{
    RtlZeroMemory(Features, sizeof(*Features));
}

ULONG
ProcArchCapabilities(
    _In_ PPROC_FEATURES Features)
{
    UNREFERENCED_PARAMETER(Features);
    return 0;
}

/* MADT GIC CPU Interface structure (ACPI 6.5, 5.2.12.14) */
BOOLEAN
ProcArchParseMat(
    _In_reads_bytes_(Length) const UCHAR *Entry,
    _In_ ULONG Length,
    _Out_ PULONGLONG HardwareId)
{
    const ULONG End = FIELD_OFFSET(ARM64_ACPI_MADT_GENERIC_INTERRUPT, Mpidr) + sizeof(ULONGLONG);

    if ((Length < End) || (Entry[0] != ARM64_ACPI_MADT_TYPE_GENERIC_INTERRUPT) || (Entry[1] < End))
        return FALSE;

    RtlCopyMemory(HardwareId, Entry + FIELD_OFFSET(ARM64_ACPI_MADT_GENERIC_INTERRUPT, Mpidr), sizeof(*HardwareId));
    *HardwareId &= PROC_MPIDR_AFFINITY;
    return TRUE;
}

ULONGLONG
ProcArchCurrentHardwareId(VOID)
{
    ULONGLONG Mpidr;

#if defined(_MSC_VER) && !defined(__clang__)
    Mpidr = (ULONGLONG)_ReadStatusReg(0x4005);
#else
    __asm__ __volatile__("mrs %0, mpidr_el1" : "=r"(Mpidr));
#endif
    return Mpidr & PROC_MPIDR_AFFINITY;
}

/* TODO: ARM64 enters its idle states through PSCI from _LPI, which is not read */
BOOLEAN
ProcArchIdleRegister(
    _In_ PPROC_FEATURES Features,
    _In_ PPROC_REGISTER Register,
    _Out_ PPROC_IDLE_METHOD Method,
    _Out_ PULONG Hint)
{
    UNREFERENCED_PARAMETER(Features);
    UNREFERENCED_PARAMETER(Register);

    *Method = ProcIdleHalt;
    *Hint = 0;
    return FALSE;
}

VOID
ProcArchIdleFfh(
    _In_ ULONG Hint,
    _In_ volatile LONG *Monitor)
{
    UNREFERENCED_PARAMETER(Hint);
    UNREFERENCED_PARAMETER(Monitor);

    ASSERT(FALSE);
    HalProcessorIdle();
}

BOOLEAN
ProcArchFlushCachesSupported(VOID)
{
    return FALSE;
}

VOID
ProcArchFlushCaches(VOID)
{
    ASSERT(FALSE);
}

/* TODO: the activity monitor counters are not offered as feedback counters */
BOOLEAN
ProcArchReadFeedback(
    _Out_ PULONGLONG Nominal,
    _Out_ PULONGLONG Actual)
{
    *Nominal = 0;
    *Actual = 0;
    return FALSE;
}

BOOLEAN
ProcArchPerfRegisters(
    _In_ PPROC_FEATURES Features,
    _Inout_ PPROC_REGISTER Control,
    _Inout_ PPROC_REGISTER Status)
{
    UNREFERENCED_PARAMETER(Features);

    return (Control->SpaceId != PROC_SPACE_FIXED_HARDWARE) && (Status->SpaceId != PROC_SPACE_FIXED_HARDWARE);
}

/* TODO: the activity monitor counters a functional fixed hardware _CPC names are not read */
BOOLEAN
ProcArchReadFfh(
    _In_ ULONGLONG Address,
    _Out_ PULONGLONG Value)
{
    UNREFERENCED_PARAMETER(Address);

    *Value = 0;
    return FALSE;
}

BOOLEAN
ProcArchWriteFfh(
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Value)
{
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Value);

    return FALSE;
}

BOOLEAN
ProcArchHwpEnable(
    _In_ PPROC_FEATURES Features,
    _Out_ PPROC_HWP Hwp)
{
    UNREFERENCED_PARAMETER(Features);

    RtlZeroMemory(Hwp, sizeof(*Hwp));
    return FALSE;
}

VOID
ProcArchHwpRequest(
    _In_ PPROC_HWP Hwp,
    _In_ UCHAR Minimum,
    _In_ UCHAR Maximum,
    _In_ UCHAR Desired,
    _In_ UCHAR EnergyPreference,
    _In_ BOOLEAN SetEnergyPreference,
    _In_ ULONG ActivityWindow)
{
    UNREFERENCED_PARAMETER(ActivityWindow);
    UNREFERENCED_PARAMETER(Hwp);
    UNREFERENCED_PARAMETER(Minimum);
    UNREFERENCED_PARAMETER(Maximum);
    UNREFERENCED_PARAMETER(Desired);
    UNREFERENCED_PARAMETER(EnergyPreference);
    UNREFERENCED_PARAMETER(SetEnergyPreference);

    ASSERT(FALSE);
}

VOID
ProcArchHwpRestore(
    _Inout_ PPROC_HWP Hwp)
{
    ASSERT(!Hwp->Saved);
}
