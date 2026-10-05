/*
 * PROJECT:     LiberNT Hardware Abstraction Layer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Secondary (GPIO) interrupt controller services for the APIC HAL
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <hal.h>
#include "apicp.h"

#define NDEBUG
#include <debug.h>

#undef HalAllocateGsivForSecondaryInterrupt
#undef HalSecondaryInterruptQueryPrimaryInformation
#undef HalIsInterruptTypeSecondary

#define HALP_SECONDARY_GSIV_BASE    0x400
#define HALP_SECONDARY_GSIV_COUNT   256
#define HALP_SECONDARY_OWNER_MAX    64
#define HALP_SECONDARY_CONTROLLERS  8
#define HALP_SECONDARY_VECTOR_MIN   0x50
#define HALP_SECONDARY_VECTOR_MAX   0xBF

typedef struct _HALP_SECONDARY_LINE
{
    CHAR OwnerName[HALP_SECONDARY_OWNER_MAX];
    USHORT OwnerNameLength;
    UCHAR Vector;
    BOOLEAN Allocated;
    BOOLEAN Applied;
    BOOLEAN Passive;
    volatile LONG Requested;
    volatile LONG Rearm;
    KINTERRUPT_MODE Mode;
} HALP_SECONDARY_LINE, *PHALP_SECONDARY_LINE;

typedef struct _HALP_SECONDARY_CONTROLLER
{
    HAL_SECONDARY_INTERRUPT_INTERFACE Interface;
    CHAR OwnerName[HALP_SECONDARY_OWNER_MAX];
    USHORT OwnerNameLength;
    BOOLEAN Registered;
} HALP_SECONDARY_CONTROLLER, *PHALP_SECONDARY_CONTROLLER;

static HALP_SECONDARY_LINE HalpSecondaryLines[HALP_SECONDARY_GSIV_COUNT];
static HALP_SECONDARY_CONTROLLER HalpSecondaryControllers[HALP_SECONDARY_CONTROLLERS];
static USHORT HalpSecondaryVectorLine[256];
static ULONG HalpSecondaryLineCount;
static KSPIN_LOCK HalpSecondaryLock;
static KDPC HalpSecondaryDpc;
static BOOLEAN HalpSecondaryReady;

static
USHORT
HalpSecondaryCopyOwner(
    _Out_writes_(HALP_SECONDARY_OWNER_MAX) PCHAR Target,
    _In_reads_bytes_opt_(Length) const CHAR *Source,
    _In_ USHORT Length)
{
    if (Source == NULL)
        Length = 0;
    if (Length > HALP_SECONDARY_OWNER_MAX - 1)
        Length = HALP_SECONDARY_OWNER_MAX - 1;
    RtlZeroMemory(Target, HALP_SECONDARY_OWNER_MAX);
    if (Length != 0)
        RtlCopyMemory(Target, Source, Length);
    return Length;
}

static
PHALP_SECONDARY_CONTROLLER
HalpSecondaryFindController(
    _In_ PHALP_SECONDARY_LINE Line)
{
    ULONG Index;

    for (Index = 0; Index < HALP_SECONDARY_CONTROLLERS; Index++)
    {
        PHALP_SECONDARY_CONTROLLER Controller = &HalpSecondaryControllers[Index];

        if (Controller->Registered &&
            Controller->OwnerNameLength == Line->OwnerNameLength &&
            RtlCompareMemory(Controller->OwnerName, Line->OwnerName, Line->OwnerNameLength) == Line->OwnerNameLength)
        {
            return Controller;
        }
    }
    return NULL;
}

static
PHALP_SECONDARY_LINE
HalpSecondaryLineFromGsiv(
    _In_ ULONG Gsiv)
{
    if (Gsiv < HALP_SECONDARY_GSIV_BASE || Gsiv >= HALP_SECONDARY_GSIV_BASE + HALP_SECONDARY_GSIV_COUNT)
        return NULL;
    if (!HalpSecondaryLines[Gsiv - HALP_SECONDARY_GSIV_BASE].Allocated)
        return NULL;
    return &HalpSecondaryLines[Gsiv - HALP_SECONDARY_GSIV_BASE];
}

static
PHALP_SECONDARY_LINE
HalpSecondaryLineFromVector(
    _In_ ULONG Vector)
{
    if (Vector >= RTL_NUMBER_OF(HalpSecondaryVectorLine) || HalpSecondaryVectorLine[Vector] == 0)
        return NULL;
    return &HalpSecondaryLines[HalpSecondaryVectorLine[Vector] - 1];
}

static
VOID
NTAPI
HalpSecondaryApplyLines(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeAcquireSpinLockAtDpcLevel(&HalpSecondaryLock);
    for (Index = 0; Index < HalpSecondaryLineCount; Index++)
    {
        PHALP_SECONDARY_LINE Line = &HalpSecondaryLines[Index];
        PHALP_SECONDARY_CONTROLLER Controller;
        BOOLEAN Requested = (Line->Requested != 0);
        LONG Rearm;

        if (!Line->Allocated)
            continue;
        if (Requested == Line->Applied && !(Requested && Line->Rearm != 0))
            continue;
        Controller = HalpSecondaryFindController(Line);
        if (Controller == NULL)
            continue;

        Rearm = InterlockedExchange(&Line->Rearm, 0);
        if (Requested)
        {
            if (NT_SUCCESS(Controller->Interface.EnableInterrupt(Controller->Interface.Context,
                                                                 HALP_SECONDARY_GSIV_BASE + Index,
                                                                 Line->Mode,
                                                                 InterruptPolarityUnknown)))
            {
                Line->Applied = TRUE;
            }
            else if (Rearm != 0)
            {
                InterlockedExchange(&Line->Rearm, 1);
            }
        }
        else
        {
            Controller->Interface.DisableInterrupt(Controller->Interface.Context, HALP_SECONDARY_GSIV_BASE + Index);
            Line->Applied = FALSE;
        }
    }
    KeReleaseSpinLockFromDpcLevel(&HalpSecondaryLock);
}

static
NTSTATUS
NTAPI
HalpAllocateGsivForSecondaryInterrupt(
    _In_reads_bytes_(OwnerNameLength) PCCHAR OwnerName,
    _In_ USHORT OwnerNameLength,
    _Out_ PULONG Gsiv)
{
    PHALP_SECONDARY_LINE Line;
    ULONG Index;
    KIRQL OldIrql;

    *Gsiv = 0;
    KeAcquireSpinLock(&HalpSecondaryLock, &OldIrql);
    if (HalpSecondaryLineCount >= HALP_SECONDARY_GSIV_COUNT)
    {
        KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Index = HalpSecondaryLineCount++;
    Line = &HalpSecondaryLines[Index];
    Line->OwnerNameLength = HalpSecondaryCopyOwner(Line->OwnerName, OwnerName, OwnerNameLength);
    Line->Mode = LevelSensitive;
    Line->Allocated = TRUE;
    KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);

    *Gsiv = HALP_SECONDARY_GSIV_BASE + Index;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HalpSecondaryInterruptQueryPrimaryInformation(
    _In_ PINTERRUPT_VECTOR_DATA VectorData,
    _Out_ PULONG PrimaryGsiv)
{
    PHALP_SECONDARY_LINE Line;
    PHALP_SECONDARY_CONTROLLER Controller;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    *PrimaryGsiv = 0;
    if (VectorData == NULL)
        return STATUS_INVALID_PARAMETER;
    Line = HalpSecondaryLineFromGsiv(VectorData->ControllerInput.Gsiv);
    if (Line == NULL)
        return STATUS_NOT_FOUND;

    KeAcquireSpinLock(&HalpSecondaryLock, &OldIrql);
    Controller = HalpSecondaryFindController(Line);
    if (Controller == NULL)
        Status = STATUS_DEVICE_NOT_READY;
    else
        *PrimaryGsiv = Controller->Interface.PrimaryGsiv;
    KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);
    return Status;
}

static
BOOLEAN
NTAPI
HalpIsInterruptTypeSecondary(
    _In_ ULONG Type,
    _In_ ULONG InputGsiv)
{
    UNREFERENCED_PARAMETER(Type);
    return HalpSecondaryLineFromGsiv(InputGsiv) != NULL;
}

static
BOOLEAN
NTAPI
HalpSecondaryDispatchInterrupt(
    _In_ ULONG Gsiv)
{
    PHALP_SECONDARY_LINE Line = HalpSecondaryLineFromGsiv(Gsiv);

    if (Line == NULL || Line->Vector == 0 || Line->Requested == 0)
        return FALSE;
    if (Line->Passive)
        return KeDispatchSecondaryInterrupt(Line->Vector, 0, NULL) && Line->Mode != LevelSensitive;
    HalpRequestSelfInterrupt(Line->Vector);
    return TRUE;
}

BOOLEAN
HalpSecondaryIsGsiv(
    _In_ ULONG Gsiv)
{
    return HalpSecondaryReady && HalpSecondaryLineFromGsiv(Gsiv) != NULL;
}

BOOLEAN
HalpSecondaryIsVector(
    _In_ ULONG Vector)
{
    return HalpSecondaryLineFromVector(Vector) != NULL;
}

ULONG
HalpSecondaryGetRootVector(
    _In_ ULONG Gsiv,
    _Out_ PKIRQL OutIrql,
    _Out_ PKAFFINITY OutAffinity)
{
    PHALP_SECONDARY_LINE Line;
    ULONG Candidate;
    KIRQL OldIrql;

    *OutIrql = 0;
    *OutAffinity = 0;
    Line = HalpSecondaryLineFromGsiv(Gsiv);
    if (Line == NULL)
        return 0;

    KeAcquireSpinLock(&HalpSecondaryLock, &OldIrql);
    if (Line->Vector == 0)
    {
        for (Candidate = HALP_SECONDARY_VECTOR_MAX; Candidate >= HALP_SECONDARY_VECTOR_MIN; Candidate--)
        {
            if (HalpVectorToIndex[Candidate] == APIC_FREE_VECTOR)
            {
                HalpVectorToIndex[Candidate] = APIC_RESERVED_VECTOR;
                HalpSecondaryVectorLine[Candidate] = (USHORT)(Line - HalpSecondaryLines + 1);
                Line->Vector = (UCHAR)Candidate;
                break;
            }
        }
    }
    KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);

    if (Line->Vector == 0)
        return 0;
    *OutIrql = HalpVectorToIrql(Line->Vector);
    *OutAffinity = HalpDefaultInterruptAffinity;
    return Line->Vector;
}

BOOLEAN
HalpSecondaryEnable(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE InterruptMode)
{
    PHALP_SECONDARY_LINE Line = HalpSecondaryLineFromVector(Vector);

    if (Line == NULL)
        return FALSE;
    Line->Passive = (Irql == PASSIVE_LEVEL);
    Line->Mode = InterruptMode;
    if (InterlockedExchange(&Line->Requested, 1) != 0 && InterruptMode == LevelSensitive)
        InterlockedExchange(&Line->Rearm, 1);
    KeInsertQueueDpc(&HalpSecondaryDpc, NULL, NULL);
    return TRUE;
}

VOID
HalpSecondaryDisable(
    _In_ ULONG Vector)
{
    PHALP_SECONDARY_LINE Line = HalpSecondaryLineFromVector(Vector);

    if (Line == NULL)
        return;
    InterlockedExchange(&Line->Requested, 0);
    KeInsertQueueDpc(&HalpSecondaryDpc, NULL, NULL);
}

VOID
HalpSecondaryInitialize(VOID)
{
    KeInitializeSpinLock(&HalpSecondaryLock);
    KeInitializeDpc(&HalpSecondaryDpc, HalpSecondaryApplyLines, NULL);
    HALPRIVATEDISPATCH->HalAllocateGsivForSecondaryInterrupt = HalpAllocateGsivForSecondaryInterrupt;
    HALPRIVATEDISPATCH->HalSecondaryInterruptQueryPrimaryInformation = HalpSecondaryInterruptQueryPrimaryInformation;
    HALPRIVATEDISPATCH->HalIsInterruptTypeSecondary = HalpIsInterruptTypeSecondary;
    HalpSecondaryReady = TRUE;
}

NTSTATUS
HalpSecondaryRegisterInterface(
    _In_ ULONG BufferSize,
    _In_ PVOID Buffer)
{
    PHAL_SECONDARY_INTERRUPT_INTERFACE Interface = Buffer;
    PHALP_SECONDARY_CONTROLLER Controller = NULL;
    ULONG Index;
    KIRQL OldIrql;

    if (!HalpSecondaryReady)
        return STATUS_NOT_SUPPORTED;
    if (Interface == NULL || BufferSize < sizeof(HAL_SECONDARY_INTERRUPT_INTERFACE) ||
        Interface->Size < sizeof(HAL_SECONDARY_INTERRUPT_INTERFACE) ||
        Interface->EnableInterrupt == NULL || Interface->DisableInterrupt == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&HalpSecondaryLock, &OldIrql);
    for (Index = 0; Index < HALP_SECONDARY_CONTROLLERS && Controller == NULL; Index++)
    {
        if (HalpSecondaryControllers[Index].Registered &&
            HalpSecondaryControllers[Index].Interface.Context == Interface->Context)
        {
            Controller = &HalpSecondaryControllers[Index];
        }
    }
    for (Index = 0; Index < HALP_SECONDARY_CONTROLLERS && Controller == NULL; Index++)
    {
        if (HalpSecondaryControllers[Index].Registered &&
            HalpSecondaryControllers[Index].OwnerNameLength == Interface->OwnerNameLength &&
            Interface->OwnerName != NULL &&
            RtlCompareMemory(HalpSecondaryControllers[Index].OwnerName, Interface->OwnerName, Interface->OwnerNameLength) == Interface->OwnerNameLength)
        {
            Controller = &HalpSecondaryControllers[Index];
        }
    }
    for (Index = 0; Index < HALP_SECONDARY_CONTROLLERS && Controller == NULL; Index++)
    {
        if (!HalpSecondaryControllers[Index].Registered)
            Controller = &HalpSecondaryControllers[Index];
    }
    if (Controller == NULL)
    {
        KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Controller->Interface = *Interface;
    Controller->OwnerNameLength = HalpSecondaryCopyOwner(Controller->OwnerName, Interface->OwnerName, Interface->OwnerNameLength);
    Controller->Interface.OwnerName = Controller->OwnerName;
    Controller->Interface.OwnerNameLength = Controller->OwnerNameLength;
    Controller->Registered = TRUE;
    KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);

    KeInsertQueueDpc(&HalpSecondaryDpc, NULL, NULL);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpSecondaryQueryInformation(
    _In_ ULONG BufferSize,
    _Out_ PVOID Buffer,
    _Out_opt_ PULONG ReturnedLength)
{
    PHAL_SECONDARY_INTERRUPT_INFORMATION Information = Buffer;

    if (!HalpSecondaryReady)
        return STATUS_NOT_SUPPORTED;
    if (ReturnedLength != NULL)
        *ReturnedLength = sizeof(HAL_SECONDARY_INTERRUPT_INFORMATION);
    if (Information == NULL || BufferSize < sizeof(HAL_SECONDARY_INTERRUPT_INFORMATION))
        return STATUS_INFO_LENGTH_MISMATCH;

    Information->Size = sizeof(HAL_SECONDARY_INTERRUPT_INFORMATION);
    Information->GsivBase = HALP_SECONDARY_GSIV_BASE;
    Information->GsivCount = HALP_SECONDARY_GSIV_COUNT;
    Information->DispatchInterrupt = HalpSecondaryDispatchInterrupt;
    return STATUS_SUCCESS;
}
