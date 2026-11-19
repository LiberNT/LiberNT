/*
 * PROJECT:     LiberNT Processor Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Processor identification and the ACPI idle and performance objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "processr.h"
#include <aux_klib.h>

#define NDEBUG
#include <debug.h>

#define PROC_OSC_REVISION           1
#define PROC_OSC_DWORDS             2
#define PROC_OSC_ERRORS             0x0000000E
#define PROC_OSC_MASKED             0x00000010
#define PROC_PDC_REVISION           1
#define PROC_CPC_MINIMUM_ENTRIES    17
#define PROC_PSD_ENTRIES            5
#define PROC_DOMAIN_PRIVATE         0x80000000

/* ACPI 6.5, 5.2.9: the fixed ACPI description table fields read here */
#define PROC_FADT_PM_TMR_BLK        76
#define PROC_FADT_PM_TMR_LEN        91
#define PROC_FADT_FLAGS             112
#define PROC_FADT_X_PM_TMR_BLK      208
#define PROC_FADT_WBINVD            0x00000001
#define PROC_FADT_HW_REDUCED        0x00100000

/* The processor vendor-specific capabilities UUID, 4077A616-290C-47BE-9EBD-D87058713953 */
static const UCHAR ProcOscUuid[16] =
{
    0x16, 0xA6, 0x77, 0x40, 0x0C, 0x29, 0xBE, 0x47, 0x9E, 0xBD, 0xD8, 0x70, 0x58, 0x71, 0x39, 0x53
};

static PROC_REGISTER ProcPmTimer;
static BOOLEAN ProcFlushCaches;

/* The PM timer read after an idle state entered through a register, and whether caches may be flushed for C3 */
VOID
ProcReadFixedTable(VOID)
{
    PUCHAR Fadt;
    ULONG Length = 0, Flags, Port;
    NTSTATUS Status;

    PAGED_CODE();

    Status = AuxKlibGetSystemFirmwareTable(PROC_METHOD('I', 'P', 'C', 'A'), PROC_METHOD('F', 'A', 'C', 'P'),
                                           NULL, 0, &Length);
    if ((Status != STATUS_BUFFER_TOO_SMALL) || (Length < PROC_FADT_FLAGS + sizeof(ULONG)))
    {
        DPRINT1("No FADT: 0x%08lx\n", Status);
        return;
    }

    Fadt = ExAllocatePoolWithTag(PagedPool, Length, PROC_TAG);
    if (Fadt == NULL)
        return;

    Status = AuxKlibGetSystemFirmwareTable(PROC_METHOD('I', 'P', 'C', 'A'), PROC_METHOD('F', 'A', 'C', 'P'),
                                           Fadt, Length, &Length);
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(&Flags, Fadt + PROC_FADT_FLAGS, sizeof(Flags));
        ProcFlushCaches = ((Flags & PROC_FADT_WBINVD) != 0) && ProcArchFlushCachesSupported();

        RtlZeroMemory(&ProcPmTimer, sizeof(ProcPmTimer));
        if (!(Flags & PROC_FADT_HW_REDUCED))
        {
            if (Length >= PROC_FADT_X_PM_TMR_BLK + 12)
            {
                ProcPmTimer.SpaceId = Fadt[PROC_FADT_X_PM_TMR_BLK];
                ProcPmTimer.BitWidth = Fadt[PROC_FADT_X_PM_TMR_BLK + 1];
                ProcPmTimer.BitOffset = Fadt[PROC_FADT_X_PM_TMR_BLK + 2];
                ProcPmTimer.AccessSize = Fadt[PROC_FADT_X_PM_TMR_BLK + 3];
                RtlCopyMemory(&ProcPmTimer.Address, Fadt + PROC_FADT_X_PM_TMR_BLK + 4, sizeof(ProcPmTimer.Address));
            }

            RtlCopyMemory(&Port, Fadt + PROC_FADT_PM_TMR_BLK, sizeof(Port));
            if ((ProcPmTimer.Address == 0) && (Port != 0) && (Fadt[PROC_FADT_PM_TMR_LEN] == sizeof(ULONG)))
            {
                ProcPmTimer.SpaceId = PROC_SPACE_SYSTEM_IO;
                ProcPmTimer.BitWidth = 32;
                ProcPmTimer.AccessSize = 3;
                ProcPmTimer.Address = Port;
            }

            ProcPmTimer.Valid = (ProcPmTimer.Address != 0) && (ProcPmTimer.BitWidth != 0) &&
                                (ProcPmTimer.SpaceId != PROC_SPACE_FIXED_HARDWARE);
            if (ProcPmTimer.Valid)
                (VOID)ProcPrepareRegister(&ProcPmTimer);
        }
    }

    ExFreePoolWithTag(Fadt, PROC_TAG);
}

/* Some chipsets stop the clock late after an idle state is entered through a register; a slow read gives them the time */
VOID
ProcIdleExitWait(VOID)
{
    if (ProcPmTimer.Valid)
        ProcTouchRegister(&ProcPmTimer);
}

VOID
ProcEnterProcessor(
    _In_ PPROC_DEVICE Device,
    _Out_ PGROUP_AFFINITY Previous)
{
    GROUP_AFFINITY Affinity;

    PAGED_CODE();
    ASSERT(Device->NumberValid);

    RtlZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Group = Device->Number.Group;
    Affinity.Mask = (KAFFINITY)1 << Device->Number.Number;
    KeSetSystemGroupAffinityThread(&Affinity, Previous);
}

VOID
ProcLeaveProcessor(
    _In_ PGROUP_AFFINITY Previous)
{
    PAGED_CODE();

    KeRevertToUserGroupAffinityThread(Previous);
}

/* The ACPI driver answers _MAT from the MADT for processors whose firmware has none */
BOOLEAN
ProcIdentify(
    _Inout_ PPROC_DEVICE Device)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT Mat;
    GROUP_AFFINITY Affinity, Previous;
    PROCESSOR_NUMBER Number;
    PROC_FEATURES Features;
    ULONGLONG HardwareId;
    ULONG Count, Index;
    BOOLEAN Match;

    PAGED_CODE();

    Device->NumberValid = FALSE;
    Device->HardwareIdValid = FALSE;

    if (!ProcQueryInteger(Device, PROC_METHOD('_', 'U', 'I', 'D'), &Device->Uid))
        Device->Uid = MAXULONG;

    if (NT_SUCCESS(ProcEvaluate(Device, PROC_METHOD('_', 'M', 'A', 'T'), &Output)))
    {
        Mat = ProcArgument(Output, 0);
        if ((Mat != NULL) && (Mat->Type == ACPI_METHOD_ARGUMENT_BUFFER))
            Device->HardwareIdValid = ProcArchParseMat(Mat->Data, Mat->DataLength, &Device->HardwareId);

        ExFreePoolWithTag(Output, PROC_TAG);
    }

    if (!Device->HardwareIdValid)
    {
        /* TODO: a processor whose _MAT is missing is not looked up in the MADT by its UID */
        DPRINT1("Processor UID %lu: no usable _MAT entry\n", Device->Uid);
        return FALSE;
    }

    Count = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    for (Index = 0; Index < Count; Index++)
    {
        if (!NT_SUCCESS(KeGetProcessorNumberFromIndex(Index, &Number)))
            continue;

        RtlZeroMemory(&Affinity, sizeof(Affinity));
        Affinity.Group = Number.Group;
        Affinity.Mask = (KAFFINITY)1 << Number.Number;
        KeSetSystemGroupAffinityThread(&Affinity, &Previous);

        HardwareId = ProcArchCurrentHardwareId();
        Match = (HardwareId == Device->HardwareId);
        if (Match)
            ProcArchQueryFeatures(&Features);

        KeRevertToUserGroupAffinityThread(&Previous);

        if (Match)
        {
            Device->Index = Index;
            Device->Number = Number;
            Device->Features = Features;
            Device->NumberValid = TRUE;
            return TRUE;
        }
    }

    DPRINT1("Processor UID %lu: hardware ID 0x%I64x is not a running processor\n", Device->Uid, Device->HardwareId);
    return FALSE;
}

/* The firmware may load the tables that describe the processor's states only once it knows what the OS supports */
VOID
ProcDeclareCapabilities(
    _Inout_ PPROC_DEVICE Device)
{
    UCHAR Storage[FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) +
                  ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ProcOscUuid)) +
                  2 * ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) +
                  ACPI_METHOD_ARGUMENT_LENGTH(3 * sizeof(ULONG))];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Input = (PACPI_EVAL_INPUT_BUFFER_COMPLEX)Storage;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG Capabilities = ProcArchCapabilities(&Device->Features);
    ULONG Buffer[3];
    NTSTATUS Status;

    PAGED_CODE();

    Device->Capabilities = 0;
    if (Capabilities == 0)
        return;

    RtlZeroMemory(Storage, sizeof(Storage));
    Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input->MethodNameAsUlong = PROC_METHOD('_', 'O', 'S', 'C');
    Input->ArgumentCount = 4;
    Argument = Input->Argument;
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument, ProcOscUuid, sizeof(ProcOscUuid));
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, PROC_OSC_REVISION);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, PROC_OSC_DWORDS);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    Buffer[0] = 0;
    Buffer[1] = Capabilities;
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument, Buffer, PROC_OSC_DWORDS * sizeof(ULONG));
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    Input->Size = (ULONG)((PUCHAR)Argument - (PUCHAR)Input->Argument);

    Status = ProcEvaluateComplex(Device, Input, (ULONG)((PUCHAR)Argument - Storage), &Output);
    if (NT_SUCCESS(Status))
    {
        Device->Capabilities = Capabilities;
        Argument = ProcArgument(Output, 0);
        if ((Argument != NULL) && (Argument->Type == ACPI_METHOD_ARGUMENT_BUFFER) &&
            (Argument->DataLength >= PROC_OSC_DWORDS * sizeof(ULONG)))
        {
            RtlCopyMemory(Buffer, Argument->Data, PROC_OSC_DWORDS * sizeof(ULONG));
            if (Buffer[0] & PROC_OSC_ERRORS)
                DPRINT1("Processor %lu: _OSC 0x%lx reports errors 0x%lx\n", Device->Index, Capabilities, Buffer[0]);
            if (Buffer[0] & PROC_OSC_MASKED)
                Device->Capabilities = Buffer[1];
        }

        ExFreePoolWithTag(Output, PROC_TAG);
        DPRINT1("Processor %lu: _OSC declared 0x%lx, granted 0x%lx\n", Device->Index, Capabilities, Device->Capabilities);
        return;
    }

    RtlZeroMemory(Storage, sizeof(Storage));
    Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input->MethodNameAsUlong = PROC_METHOD('_', 'P', 'D', 'C');
    Input->ArgumentCount = 1;
    Buffer[0] = PROC_PDC_REVISION;
    Buffer[1] = 1;
    Buffer[2] = Capabilities;
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Input->Argument, Buffer, sizeof(Buffer));
    Input->Size = ACPI_METHOD_ARGUMENT_LENGTH(sizeof(Buffer));

    Status = ProcEvaluateComplex(Device,
                                 Input,
                                 FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) + Input->Size,
                                 &Output);
    if (NT_SUCCESS(Status))
    {
        Device->Capabilities = Capabilities;
        ExFreePoolWithTag(Output, PROC_TAG);
    }

    DPRINT1("Processor %lu: _PDC declared 0x%lx, status 0x%08lx\n", Device->Index, Capabilities, Status);
}

VOID
ProcParseIdleStates(
    _Inout_ PPROC_DEVICE Device)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT Package;
    PPROC_IDLE_STATE State;
    PROC_REGISTER Register;
    ULONG Declared, Index;

    PAGED_CODE();

    Device->IdleCount = 0;

    if (!NT_SUCCESS(ProcEvaluate(Device, PROC_METHOD('_', 'C', 'S', 'T'), &Output)))
        return;

    if (!ProcInteger(ProcArgument(Output, 0), &Declared))
        Declared = 0;

    for (Index = 1; (Index <= Declared) && (Device->IdleCount < PROC_MAX_IDLE_STATES); Index++)
    {
        Package = ProcArgument(Output, Index);
        State = &Device->Idle[Device->IdleCount];
        RtlZeroMemory(State, sizeof(*State));

        if (!ProcInteger(ProcPackageElement(Package, 1), &State->Type) ||
            !ProcInteger(ProcPackageElement(Package, 2), &State->Latency) ||
            !ProcInteger(ProcPackageElement(Package, 3), &State->Power) ||
            (State->Type < 1) || (State->Type > 3))
        {
            continue;
        }

        if (!ProcParseRegister(ProcPackageElement(Package, 0), &Register))
        {
            if (State->Type != 1)
                continue;

            State->Method = ProcIdleHalt;
        }
        else if (Register.SpaceId == PROC_SPACE_FIXED_HARDWARE)
        {
            if (!ProcArchIdleRegister(&Device->Features, &Register, &State->Method, &State->Hint))
            {
                if (State->Type != 1)
                    continue;

                State->Method = ProcIdleHalt;
            }
        }
        else if (State->Type == 1)
        {
            State->Method = ProcIdleHalt;
        }
        else if (State->Type == 2)
        {
            if (!ProcPrepareRegister(&Register))
                continue;

            State->Register = Register;
            State->Method = (Register.SpaceId == PROC_SPACE_SYSTEM_IO) ? ProcIdleIo : ProcIdleMemory;
        }
        else
        {
            /* ACPI 6.5, 8.1.4: on more than one processor, C3 keeps caches coherent by flushing them before it is entered */
            if (!ProcFlushCaches || !ProcPrepareRegister(&Register))
                continue;

            State->Register = Register;
            State->FlushCaches = TRUE;
            State->Method = (Register.SpaceId == PROC_SPACE_SYSTEM_IO) ? ProcIdleIo : ProcIdleMemory;
        }

        DPRINT1("Processor %lu: C%lu %s hint 0x%lx latency %lu us\n",
                Device->Index,
                State->Type,
                (State->Method == ProcIdleFfh) ? "FFH" : (State->Method == ProcIdleHalt) ? "halt" : "register",
                State->Hint,
                State->Latency);
        Device->IdleCount++;
    }

    ExFreePoolWithTag(Output, PROC_TAG);
}

static
VOID
ProcParseDomain(
    _Inout_ PPROC_DEVICE Device)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT Package;
    ULONG Entries, Revision, Domain, Coordination, Processors;

    PAGED_CODE();

    Device->DomainId = PROC_DOMAIN_PRIVATE | Device->Index;
    Device->DomainMembers = 1;
    Device->Coordination = PROCESSOR_DOMAIN_COORDINATION_HW_ALL;

    if (!NT_SUCCESS(ProcEvaluate(Device, PROC_METHOD('_', 'P', 'S', 'D'), &Output)))
        return;

    Package = ProcArgument(Output, 0);
    if (ProcInteger(ProcPackageElement(Package, 0), &Entries) && (Entries >= PROC_PSD_ENTRIES) &&
        ProcInteger(ProcPackageElement(Package, 1), &Revision) && (Revision == 0) &&
        ProcInteger(ProcPackageElement(Package, 2), &Domain) && (Domain < PROC_DOMAIN_PRIVATE) &&
        ProcInteger(ProcPackageElement(Package, 3), &Coordination) &&
        ProcInteger(ProcPackageElement(Package, 4), &Processors) && (Processors != 0))
    {
        switch (Coordination)
        {
            case ACPI_PPM_SOFTWARE_ALL:
                Device->Coordination = PROCESSOR_DOMAIN_COORDINATION_SW_ALL;
                break;

            case ACPI_PPM_SOFTWARE_ANY:
                Device->Coordination = PROCESSOR_DOMAIN_COORDINATION_SW_ANY;
                break;

            case ACPI_PPM_HARDWARE_ALL:
                Device->Coordination = PROCESSOR_DOMAIN_COORDINATION_HW_ALL;
                break;

            default:
                DPRINT1("Processor %lu: _PSD coordination 0x%lx is not defined\n", Device->Index, Coordination);
                ExFreePoolWithTag(Output, PROC_TAG);
                return;
        }

        Device->DomainId = Domain;
        Device->DomainMembers = Processors;
    }

    ExFreePoolWithTag(Output, PROC_TAG);
}

static
BOOLEAN
ProcCppcValue(
    _In_ PPROC_DEVICE Device,
    _In_opt_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PULONG Value)
{
    GROUP_AFFINITY Previous;
    PROC_REGISTER Register;
    ULONGLONG Read;
    BOOLEAN Found;

    PAGED_CODE();

    if (ProcInteger(Argument, Value))
        return TRUE;

    if (!ProcParseRegister(Argument, &Register) || !ProcPrepareRegister(&Register))
        return FALSE;

    ProcEnterProcessor(Device, &Previous);
    Found = ProcReadRegister(&Register, &Read);
    ProcLeaveProcessor(&Previous);
    ProcReleaseRegister(&Register);

    if (!Found || (Read > MAXULONG))
        return FALSE;

    *Value = (ULONG)Read;
    return TRUE;
}

static
VOID
ProcOptionalRegister(
    _In_opt_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PPROC_REGISTER Register)
{
    PAGED_CODE();

    if (!ProcParseRegister(Argument, Register) ||
        ((Register->SpaceId == PROC_SPACE_FIXED_HARDWARE) && (Register->Address == 0)) ||
        !ProcPrepareRegister(Register))
    {
        RtlZeroMemory(Register, sizeof(*Register));
    }
}

/* Collaborative Processor Performance Control (ACPI 6.5, 8.4.6.1) */
static
BOOLEAN
ProcParseCppc(
    _Inout_ PPROC_DEVICE Device)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PPROC_CPPC Cppc = &Device->Cppc;
    ULONG Entries, Value;
    BOOLEAN Valid = FALSE;

    PAGED_CODE();

    if (!NT_SUCCESS(ProcEvaluate(Device, PROC_METHOD('_', 'C', 'P', 'C'), &Output)))
        return FALSE;

    if (!ProcInteger(ProcArgument(Output, 0), &Entries) || (Entries < PROC_CPC_MINIMUM_ENTRIES) ||
        !ProcCppcValue(Device, ProcArgument(Output, 2), &Device->Highest) ||
        !ProcCppcValue(Device, ProcArgument(Output, 3), &Device->Nominal) ||
        !ProcCppcValue(Device, ProcArgument(Output, 4), &Device->LowestNonlinear) ||
        !ProcCppcValue(Device, ProcArgument(Output, 5), &Device->Lowest) ||
        (Device->Highest == 0) || (Device->Lowest > Device->Highest))
    {
        goto Exit;
    }

    if ((Device->Nominal == 0) || (Device->Nominal > Device->Highest) || (Device->Nominal < Device->Lowest))
        Device->Nominal = Device->Highest;

    Device->LowestNonlinear = min(max(Device->LowestNonlinear, Device->Lowest), Device->Nominal);

    if (!ProcCppcValue(Device, ProcArgument(Output, 6), &Device->Guaranteed) ||
        (Device->Guaranteed == 0) || (Device->Guaranteed > Device->Highest))
    {
        Device->Guaranteed = Device->Nominal;
    }

    ProcOptionalRegister(ProcArgument(Output, 7), &Cppc->Desired);
    if (!Cppc->Desired.Valid)
        goto Exit;

    ProcOptionalRegister(ProcArgument(Output, 8), &Cppc->Minimum);
    ProcOptionalRegister(ProcArgument(Output, 9), &Cppc->Maximum);
    ProcOptionalRegister(ProcArgument(Output, 16), &Cppc->Enable);

    if (ProcInteger(ProcArgument(Output, 17), &Value))
        Cppc->AutonomousFixed = (Value != 0);
    else
        ProcOptionalRegister(ProcArgument(Output, 17), &Cppc->Autonomous);

    if (Entries > 19)
        ProcOptionalRegister(ProcArgument(Output, 19), &Cppc->EnergyPreference);

    Device->EppSupported = Cppc->EnergyPreference.Valid;
    Device->AutonomousSupported = Cppc->Autonomous.Valid || Cppc->AutonomousFixed;
    Device->PerfMethod = ProcPerfCppc;
    Valid = TRUE;

Exit:
    ExFreePoolWithTag(Output, PROC_TAG);
    if (!Valid)
    {
        ProcReleaseRegister(&Cppc->Desired);
        ProcReleaseRegister(&Cppc->Minimum);
        ProcReleaseRegister(&Cppc->Maximum);
        ProcReleaseRegister(&Cppc->Enable);
        ProcReleaseRegister(&Cppc->Autonomous);
        ProcReleaseRegister(&Cppc->EnergyPreference);
    }

    return Valid;
}

BOOLEAN
ProcRefreshPpc(
    _Inout_ PPROC_DEVICE Device)
{
    ULONG Ppc = 0;

    PAGED_CODE();

    if (Device->PerfMethod != ProcPerfPss)
        return FALSE;

    if (!ProcQueryInteger(Device, PROC_METHOD('_', 'P', 'P', 'C'), &Ppc) || (Ppc >= Device->PssCount))
        Ppc = 0;

    if (Ppc == Device->Ppc)
        return FALSE;

    Device->Ppc = Ppc;
    return TRUE;
}

/* ACPI 6.5, 8.4.5: the states run from the fastest; Intel lists its turbo state 1 MHz above the fastest guaranteed one */
static
BOOLEAN
ProcParsePss(
    _Inout_ PPROC_DEVICE Device)
{
    PACPI_EVAL_OUTPUT_BUFFER Pct, Pss;
    PACPI_METHOD_ARGUMENT Package;
    PPROC_PSS_STATE State;
    ULONG Index;
    BOOLEAN Valid = FALSE;

    PAGED_CODE();

    if (!NT_SUCCESS(ProcEvaluate(Device, PROC_METHOD('_', 'P', 'C', 'T'), &Pct)))
        return FALSE;

    if (!ProcParseRegister(ProcArgument(Pct, 0), &Device->PerfControl) ||
        !ProcParseRegister(ProcArgument(Pct, 1), &Device->PerfStatus) ||
        !ProcArchPerfRegisters(&Device->Features, &Device->PerfControl, &Device->PerfStatus) ||
        !ProcPrepareRegister(&Device->PerfControl) ||
        !ProcPrepareRegister(&Device->PerfStatus))
    {
        goto ExitPct;
    }

    if (!NT_SUCCESS(ProcEvaluate(Device, PROC_METHOD('_', 'P', 'S', 'S'), &Pss)))
        goto ExitPct;

    Device->PssCount = 0;
    Device->TransitionLatency = 0;
    for (Index = 0; (Index < Pss->Count) && (Device->PssCount < PROC_MAX_PERF_STATES); Index++)
    {
        Package = ProcArgument(Pss, Index);
        State = &Device->Pss[Device->PssCount];

        if (!ProcInteger(ProcPackageElement(Package, 0), &State->Frequency) ||
            !ProcInteger(ProcPackageElement(Package, 1), &State->Power) ||
            !ProcInteger(ProcPackageElement(Package, 2), &State->Latency) ||
            !ProcInteger(ProcPackageElement(Package, 3), &State->BusMasterLatency) ||
            !ProcInteger(ProcPackageElement(Package, 4), &State->Control) ||
            !ProcInteger(ProcPackageElement(Package, 5), &State->Status) ||
            (State->Frequency == 0) ||
            ((Device->PssCount != 0) && (State->Frequency >= Device->Pss[Device->PssCount - 1].Frequency)))
        {
            continue;
        }

        Device->TransitionLatency = max(Device->TransitionLatency, State->Latency);
        Device->PssCount++;
    }

    ExFreePoolWithTag(Pss, PROC_TAG);

    if (Device->PssCount == 0)
        goto ExitPct;

    Device->Highest = Device->Pss[0].Frequency;
    Device->Nominal = Device->Highest;
    if ((Device->PssCount > 1) && (Device->Pss[0].Frequency == Device->Pss[1].Frequency + 1))
        Device->Nominal = Device->Pss[1].Frequency;

    Device->Lowest = Device->Pss[Device->PssCount - 1].Frequency;
    Device->LowestNonlinear = Device->Lowest;
    Device->Guaranteed = Device->Nominal;
    Device->Ppc = 0;
    Device->PerfMethod = ProcPerfPss;
    (VOID)ProcRefreshPpc(Device);
    Valid = TRUE;

ExitPct:
    ExFreePoolWithTag(Pct, PROC_TAG);
    if (!Valid)
    {
        ProcReleaseRegister(&Device->PerfControl);
        ProcReleaseRegister(&Device->PerfStatus);
        Device->PssCount = 0;
    }

    return Valid;
}

VOID
ProcParsePerformance(
    _Inout_ PPROC_DEVICE Device)
{
    GROUP_AFFINITY Previous;
    BOOLEAN Hwp;

    PAGED_CODE();

    Device->PerfMethod = ProcPerfNone;
    Device->EppSupported = FALSE;
    Device->AutonomousSupported = FALSE;
    ProcParseDomain(Device);

    if (Device->Features.Hwp)
    {
        ProcEnterProcessor(Device, &Previous);
        Hwp = ProcArchHwpEnable(&Device->Features, &Device->Hwp);
        ProcLeaveProcessor(&Previous);

        if (Hwp)
        {
            Device->Highest = Device->Hwp.Highest;
            Device->Lowest = Device->Hwp.Lowest;
            Device->Nominal = ((Device->Hwp.Guaranteed >= Device->Lowest) && (Device->Hwp.Guaranteed <= Device->Highest)) ?
                              Device->Hwp.Guaranteed : Device->Highest;
            Device->LowestNonlinear = min(max((ULONG)Device->Hwp.Efficient, Device->Lowest), Device->Nominal);
            Device->Guaranteed = Device->Nominal;
            Device->EppSupported = Device->Features.HwpEpp;
            Device->AutonomousSupported = TRUE;
            Device->DomainId = PROC_DOMAIN_PRIVATE | Device->Index;
            Device->DomainMembers = 1;
            Device->Coordination = PROCESSOR_DOMAIN_COORDINATION_HW_ALL;
            Device->PerfMethod = ProcPerfHwp;
        }
    }

    if ((Device->PerfMethod == ProcPerfNone) && !ProcParseCppc(Device))
        (VOID)ProcParsePss(Device);

    if (Device->PerfMethod != ProcPerfNone)
    {
        DPRINT1("Processor %lu: %s performance %lu-%lu-%lu (nonlinear %lu), %lu states, _PPC %lu, domain 0x%lx/%lu type %u\n",
                Device->Index,
                (Device->PerfMethod == ProcPerfHwp) ? "HWP" : (Device->PerfMethod == ProcPerfCppc) ? "CPPC" : "_PSS",
                Device->Lowest,
                Device->Nominal,
                Device->Highest,
                Device->LowestNonlinear,
                Device->PssCount,
                Device->Ppc,
                Device->DomainId,
                Device->DomainMembers,
                Device->Coordination);
    }
    else
    {
        DPRINT1("Processor %lu: no performance control (HWP %u, EST %u)\n",
                Device->Index, Device->Features.Hwp, Device->Features.Est);
    }
}

VOID
ProcReleaseTables(
    _Inout_ PPROC_DEVICE Device)
{
    ULONG Index;

    PAGED_CODE();

    for (Index = 0; Index < Device->IdleCount; Index++)
        ProcReleaseRegister(&Device->Idle[Index].Register);

    Device->IdleCount = 0;
    ProcReleaseRegister(&Device->PerfControl);
    ProcReleaseRegister(&Device->PerfStatus);
    ProcReleaseRegister(&Device->Cppc.Desired);
    ProcReleaseRegister(&Device->Cppc.Minimum);
    ProcReleaseRegister(&Device->Cppc.Maximum);
    ProcReleaseRegister(&Device->Cppc.Enable);
    ProcReleaseRegister(&Device->Cppc.Autonomous);
    ProcReleaseRegister(&Device->Cppc.EnergyPreference);
    Device->PssCount = 0;
    Device->PerfMethod = ProcPerfNone;
}
