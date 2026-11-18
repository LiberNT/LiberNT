/*
 * PROJECT:     LiberNT Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     x86 processor identity, MWAIT idle states, P-state MSRs and HWP
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "processr.h"

#define NDEBUG
#include <debug.h>

#define PROC_MSR_MPERF                  0xE7
#define PROC_MSR_APERF                  0xE8
#define PROC_MSR_PERF_CTL               0x199
#define PROC_MSR_AMD_PSTATE_CTL         0xC0010062
#define PROC_MSR_AMD_PSTATE_STATUS      0xC0010063
#define PROC_MSR_PM_ENABLE              0x770
#define PROC_MSR_HWP_CAPABILITIES       0x771
#define PROC_MSR_HWP_REQUEST            0x774

#define PROC_HWP_ENABLE                 0x1ULL
#define PROC_HWP_REQUEST_FIELDS         0xFFFFFFFFULL
#define PROC_HWP_REQUEST_EPP            0xFF000000ULL
#define PROC_HWP_PACKAGE_CONTROL        (1ULL << 42)
#define PROC_HWP_WINDOW_SHIFT           32
#define PROC_HWP_WINDOW_MASK            (0x3FFULL << PROC_HWP_WINDOW_SHIFT)
#define PROC_HWP_WINDOW_MANTISSA        0x7F
#define PROC_HWP_WINDOW_EXPONENTS       8

#define PROC_MADT_LOCAL_APIC            0
#define PROC_MADT_LOCAL_X2APIC          9

/* Capability bits of the processor vendor-specific _OSC and _PDC (Intel Processor Vendor-Specific ACPI) */
#define PROC_CAP_P_FFH                  0x0001
#define PROC_CAP_C1_HALT                0x0002
#define PROC_CAP_SMP_C1PT               0x0008
#define PROC_CAP_SMP_C2C3               0x0010
#define PROC_CAP_SMP_P_SWCOORD          0x0020
#define PROC_CAP_C1_FFH                 0x0100
#define PROC_CAP_C2C3_FFH               0x0200
#define PROC_CAP_SMP_P_HWCOORD          0x0800
#define PROC_CAP_COLLAB_PROC_PERF       0x1000

/* Intel and AMD encode a native C-state in a functional fixed hardware register as class 2, the MWAIT hint as address */
#define PROC_FFH_CLASS_NATIVE_CSTATE    2
#define PROC_MWAIT_SUBSTATE_BITS        4
#define PROC_MWAIT_SUBSTATE_MASK        0xF

static
BOOLEAN
ProcVendorIs(
    _In_ const int *Registers,
    _In_ ULONG Ebx,
    _In_ ULONG Edx,
    _In_ ULONG Ecx)
{
    return ((ULONG)Registers[1] == Ebx) && ((ULONG)Registers[3] == Edx) && ((ULONG)Registers[2] == Ecx);
}

VOID
ProcArchQueryFeatures(
    _Out_ PPROC_FEATURES Features)
{
    int Registers[4];
    ULONG MaximumLeaf, MaximumExtended;

    RtlZeroMemory(Features, sizeof(*Features));

    __cpuid(Registers, 0);
    MaximumLeaf = (ULONG)Registers[0];
    Features->Intel = ProcVendorIs(Registers, 0x756E6547, 0x49656E69, 0x6C65746E);
    Features->Amd = ProcVendorIs(Registers, 0x68747541, 0x69746E65, 0x444D4163);

    if (MaximumLeaf >= 1)
    {
        __cpuid(Registers, 1);
        Features->Est = Features->Intel && ((Registers[2] & (1 << 7)) != 0);

        if (((Registers[2] & (1 << 3)) != 0) && (MaximumLeaf >= 5))
        {
            __cpuid(Registers, 5);
            Features->Mwait = ((Registers[2] & 1) != 0);
            Features->MwaitBreakOnInterrupt = ((Registers[2] & 2) != 0);
            Features->MwaitSubstates = (ULONG)Registers[3];
        }
    }

    if (MaximumLeaf >= 6)
    {
        /* CPUID.06H:ECX[0] reports IA32_MPERF and IA32_APERF on Intel and AMD alike */
        __cpuid(Registers, 6);
        Features->Feedback = ((Registers[2] & 1) != 0);
        Features->Hwp = Features->Intel && ((Registers[0] & (1 << 7)) != 0);
        Features->HwpEpp = Features->Hwp && ((Registers[0] & (1 << 10)) != 0);
    }

    if (Features->Amd)
    {
        __cpuid(Registers, 0x80000000);
        MaximumExtended = (ULONG)Registers[0];
        if (MaximumExtended >= 0x80000007)
        {
            __cpuid(Registers, 0x80000007);
            Features->Est = ((Registers[3] & (1 << 7)) != 0);
        }
    }
}

ULONG
ProcArchCapabilities(
    _In_ PPROC_FEATURES Features)
{
    ULONG Capabilities = PROC_CAP_C1_HALT | PROC_CAP_SMP_C1PT | PROC_CAP_SMP_C2C3;

    if (Features->Est)
        Capabilities |= PROC_CAP_P_FFH | PROC_CAP_SMP_P_SWCOORD | PROC_CAP_SMP_P_HWCOORD;

    if (Features->Mwait && Features->MwaitBreakOnInterrupt)
        Capabilities |= PROC_CAP_C1_FFH | PROC_CAP_C2C3_FFH;

    if (Features->Hwp)
        Capabilities |= PROC_CAP_COLLAB_PROC_PERF;

    return Capabilities;
}

/* MADT Processor Local APIC and Local x2APIC structures (ACPI 6.5, 5.2.12.2 and 5.2.12.12) */
BOOLEAN
ProcArchParseMat(
    _In_reads_bytes_(Length) const UCHAR *Entry,
    _In_ ULONG Length,
    _Out_ PULONGLONG HardwareId)
{
    ULONG Id;

    if ((Length >= 8) && (Entry[0] == PROC_MADT_LOCAL_APIC) && (Entry[1] >= 8))
    {
        *HardwareId = Entry[3];
        return TRUE;
    }

    if ((Length >= 16) && (Entry[0] == PROC_MADT_LOCAL_X2APIC) && (Entry[1] >= 16))
    {
        RtlCopyMemory(&Id, &Entry[4], sizeof(Id));
        *HardwareId = Id;
        return TRUE;
    }

    return FALSE;
}

ULONGLONG
ProcArchCurrentHardwareId(VOID)
{
    int Registers[4];

    __cpuid(Registers, 0);
    if ((ULONG)Registers[0] >= 0xB)
    {
        __cpuidex(Registers, 0xB, 0);
        if (Registers[1] != 0)
            return (ULONG)Registers[3];
    }

    __cpuid(Registers, 1);
    return (ULONG)Registers[1] >> 24;
}

BOOLEAN
ProcArchIdleRegister(
    _In_ PPROC_FEATURES Features,
    _In_ PPROC_REGISTER Register,
    _Out_ PPROC_IDLE_METHOD Method,
    _Out_ PULONG Hint)
{
    ULONG CState, Substates;

    *Method = ProcIdleHalt;
    *Hint = 0;

    if ((Register->BitOffset != PROC_FFH_CLASS_NATIVE_CSTATE) || (Register->Address > MAXULONG) ||
        !Features->Mwait || !Features->MwaitBreakOnInterrupt)
    {
        return FALSE;
    }

    CState = ((((ULONG)Register->Address >> PROC_MWAIT_SUBSTATE_BITS) & PROC_MWAIT_SUBSTATE_MASK) + 1) &
             PROC_MWAIT_SUBSTATE_MASK;
    if (CState >= 8)
        return FALSE;

    Substates = (Features->MwaitSubstates >> (CState * PROC_MWAIT_SUBSTATE_BITS)) & PROC_MWAIT_SUBSTATE_MASK;
    if (Substates <= ((ULONG)Register->Address & PROC_MWAIT_SUBSTATE_MASK))
        return FALSE;

    *Method = ProcIdleFfh;
    *Hint = (ULONG)Register->Address;
    return TRUE;
}

/* Interrupts stay disabled: MWAIT breaks on an interrupt anyway, which runs once the idle loop enables them */
#if defined(__clang__)
__attribute__((target("sse3")))
#elif defined(__GNUC__)
__attribute__((target("mwait")))
#endif
VOID
ProcArchIdleFfh(
    _In_ ULONG Hint,
    _In_ volatile LONG *Monitor)
{
#if defined(__clang__) || defined(__GNUC__)
    __builtin_ia32_monitor((PVOID)Monitor, 0, 0);
    __builtin_ia32_mwait(1, Hint);
#else
    _mm_monitor((PVOID)Monitor, 0, 0);
    _mm_mwait(1, Hint);
#endif
}

BOOLEAN
ProcArchFlushCachesSupported(VOID)
{
    return TRUE;
}

VOID
ProcArchFlushCaches(VOID)
{
    __wbinvd();
}

/* MPERF counts at the nominal rate and APERF at the actual one, both only while the processor runs */
BOOLEAN
ProcArchReadFeedback(
    _Out_ PULONGLONG Nominal,
    _Out_ PULONGLONG Actual)
{
    *Nominal = __readmsr(PROC_MSR_MPERF);
    *Actual = __readmsr(PROC_MSR_APERF);
    return TRUE;
}

/* A functional fixed hardware _PCT names the vendor's P-state control MSR */
BOOLEAN
ProcArchPerfRegisters(
    _In_ PPROC_FEATURES Features,
    _Inout_ PPROC_REGISTER Control,
    _Inout_ PPROC_REGISTER Status)
{
    if ((Control->SpaceId != PROC_SPACE_FIXED_HARDWARE) && (Status->SpaceId != PROC_SPACE_FIXED_HARDWARE))
        return TRUE;

    if ((Control->SpaceId != PROC_SPACE_FIXED_HARDWARE) || (Status->SpaceId != PROC_SPACE_FIXED_HARDWARE) ||
        !Features->Est)
    {
        return FALSE;
    }

    RtlZeroMemory(Control, sizeof(*Control));
    Control->Valid = TRUE;
    Control->SpaceId = PROC_SPACE_FIXED_HARDWARE;
    Control->AccessSize = 4;

    if (Features->Intel)
    {
        Control->Address = PROC_MSR_PERF_CTL;
        Control->BitWidth = 16;
        *Status = *Control;
        return TRUE;
    }

    ASSERT(Features->Amd);
    Control->Address = PROC_MSR_AMD_PSTATE_CTL;
    Control->BitWidth = 3;
    *Status = *Control;
    Status->Address = PROC_MSR_AMD_PSTATE_STATUS;
    return TRUE;
}

BOOLEAN
ProcArchReadFfh(
    _In_ ULONGLONG Address,
    _Out_ PULONGLONG Value)
{
    if (Address > MAXULONG)
        return FALSE;

    *Value = __readmsr((ULONG)Address);
    return TRUE;
}

BOOLEAN
ProcArchWriteFfh(
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Value)
{
    if (Address > MAXULONG)
        return FALSE;

    __writemsr((ULONG)Address, Value);
    return TRUE;
}

/* Once enabled, HWP stays enabled until the processor resets */
BOOLEAN
ProcArchHwpEnable(
    _In_ PPROC_FEATURES Features,
    _Out_ PPROC_HWP Hwp)
{
    ULONGLONG Enable, Capabilities;

    RtlZeroMemory(Hwp, sizeof(*Hwp));
    if (!Features->Hwp)
        return FALSE;

    Enable = __readmsr(PROC_MSR_PM_ENABLE);
    if ((Enable & PROC_HWP_ENABLE) == 0)
    {
        __writemsr(PROC_MSR_PM_ENABLE, Enable | PROC_HWP_ENABLE);
        if ((__readmsr(PROC_MSR_PM_ENABLE) & PROC_HWP_ENABLE) == 0)
            return FALSE;
    }

    Capabilities = __readmsr(PROC_MSR_HWP_CAPABILITIES);
    Hwp->Highest = (UCHAR)Capabilities;
    Hwp->Guaranteed = (UCHAR)(Capabilities >> 8);
    Hwp->Efficient = (UCHAR)(Capabilities >> 16);
    Hwp->Lowest = (UCHAR)(Capabilities >> 24);
    if ((Hwp->Highest == 0) || (Hwp->Lowest > Hwp->Highest))
    {
        DPRINT1("HWP capabilities 0x%I64x are not usable\n", Capabilities);
        return FALSE;
    }

    Hwp->SavedRequest = __readmsr(PROC_MSR_HWP_REQUEST);
    Hwp->Saved = TRUE;
    return TRUE;
}

/* Intel SDM: the activity window is a 7-bit mantissa times ten to a 3-bit exponent, in microseconds; zero leaves it to the hardware */
static
ULONGLONG
ProcHwpActivityWindow(
    _In_ ULONG Microseconds)
{
    ULONG Exponent = 0;

    while ((Microseconds > PROC_HWP_WINDOW_MANTISSA) && (Exponent + 1 < PROC_HWP_WINDOW_EXPONENTS))
    {
        Microseconds = (Microseconds + 5) / 10;
        Exponent++;
    }

    Microseconds = min(Microseconds, (ULONG)PROC_HWP_WINDOW_MANTISSA);
    return ((ULONGLONG)Microseconds | ((ULONGLONG)Exponent << 7)) << PROC_HWP_WINDOW_SHIFT;
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
    ULONGLONG Request;

    UNREFERENCED_PARAMETER(Hwp);

    Request = __readmsr(PROC_MSR_HWP_REQUEST);
    Request &= ~(PROC_HWP_PACKAGE_CONTROL | PROC_HWP_WINDOW_MASK |
                 (SetEnergyPreference ? PROC_HWP_REQUEST_FIELDS : (PROC_HWP_REQUEST_FIELDS & ~PROC_HWP_REQUEST_EPP)));
    Request |= (ULONGLONG)Minimum | ((ULONGLONG)Maximum << 8) | ((ULONGLONG)Desired << 16);
    if (SetEnergyPreference)
        Request |= (ULONGLONG)EnergyPreference << 24;
    Request |= ProcHwpActivityWindow(ActivityWindow);

    __writemsr(PROC_MSR_HWP_REQUEST, Request);
}

VOID
ProcArchHwpRestore(
    _Inout_ PPROC_HWP Hwp)
{
    if (!Hwp->Saved)
        return;

    __writemsr(PROC_MSR_HWP_REQUEST, Hwp->SavedRequest);
    Hwp->Saved = FALSE;
}
