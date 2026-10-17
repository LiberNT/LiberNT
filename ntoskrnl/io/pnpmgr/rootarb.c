/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Root arbiters of the PnP manager for ports, memory, interrupts, DMA and bus numbers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

static ARBITER_INSTANCE IopRootPortArbiter;
static ARBITER_INSTANCE IopRootMemArbiter;
static ARBITER_INSTANCE IopRootIrqArbiter;
static ARBITER_INSTANCE IopRootDmaArbiter;
static ARBITER_INSTANCE IopRootBusNumberArbiter;

static
NTSTATUS
NTAPI
IopArbPortUnpackRequirements(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PUINT64 OutMinimumAddress,
    _Out_ PUINT64 OutMaximumAddress,
    _Out_ PUINT64 OutLength,
    _Out_ PUINT64 OutAlignment)
{
    PAGED_CODE();

    if (IoDescriptor->Type != CmResourceTypePort)
        return STATUS_INVALID_PARAMETER;

    *OutMinimumAddress = (UINT64)IoDescriptor->u.Port.MinimumAddress.QuadPart;
    *OutMaximumAddress = (UINT64)IoDescriptor->u.Port.MaximumAddress.QuadPart;
    *OutLength = IoDescriptor->u.Port.Length;
    *OutAlignment = IoDescriptor->u.Port.Alignment ? IoDescriptor->u.Port.Alignment : 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopArbPortPackResource(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ UINT64 Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    PAGED_CODE();

    if (IoDescriptor->Type != CmResourceTypePort)
        return STATUS_INVALID_PARAMETER;

    CmDescriptor->Type = CmResourceTypePort;
    CmDescriptor->ShareDisposition = IoDescriptor->ShareDisposition;
    CmDescriptor->Flags = IoDescriptor->Flags;
    CmDescriptor->u.Port.Start.QuadPart = (LONGLONG)Start;
    CmDescriptor->u.Port.Length = IoDescriptor->u.Port.Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopArbPortUnpackResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PUINT64 Start,
    _Out_ PUINT64 OutLength)
{
    PAGED_CODE();

    if (CmDescriptor->Type != CmResourceTypePort)
        return STATUS_INVALID_PARAMETER;

    *Start = (UINT64)CmDescriptor->u.Port.Start.QuadPart;
    *OutLength = CmDescriptor->u.Port.Length;
    return STATUS_SUCCESS;
}

static
INT32
NTAPI
IopArbPortScoreRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    UINT64 Minimum, Maximum, Length, Alignment, Positions;

    PAGED_CODE();

    if (!NT_SUCCESS(IopArbPortUnpackRequirements(IoDescriptor, &Minimum, &Maximum, &Length, &Alignment)) ||
        Length == 0 || Maximum < Minimum || Maximum - Minimum < Length - 1)
    {
        return 0;
    }

    Positions = (Maximum - Minimum - (Length - 1)) / Alignment + 1;
    return Positions > MAXLONG ? MAXLONG : (INT32)Positions;
}

static
NTSTATUS
NTAPI
IopArbPortInitialize(VOID)
{
    NTSTATUS Status;

    PAGED_CODE();

    IopRootPortArbiter.UnpackRequirement = IopArbPortUnpackRequirements;
    IopRootPortArbiter.PackResource = IopArbPortPackResource;
    IopRootPortArbiter.UnpackResource = IopArbPortUnpackResource;
    IopRootPortArbiter.ScoreRequirement = IopArbPortScoreRequirement;

    Status = ArbInitializeArbiterInstance(&IopRootPortArbiter,
                                          NULL,
                                          CmResourceTypePort,
                                          L"RootPort",
                                          L"Root",
                                          NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopArbPortInitialize: Failed with %X\n", Status);
    }

    return Status;
}

static
NTSTATUS
NTAPI
IopArbMemUnpackRequirements(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PUINT64 OutMinimumAddress,
    _Out_ PUINT64 OutMaximumAddress,
    _Out_ PUINT64 OutLength,
    _Out_ PUINT64 OutAlignment)
{
    PAGED_CODE();

    if (IoDescriptor->Type != CmResourceTypeMemory && IoDescriptor->Type != CmResourceTypeMemoryLarge)
        return STATUS_INVALID_PARAMETER;

    *OutLength = RtlIoDecodeMemIoResource(IoDescriptor, OutAlignment, OutMinimumAddress, OutMaximumAddress);
    if (*OutAlignment == 0)
        *OutAlignment = 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopArbMemPackResource(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ UINT64 Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    UINT64 Length;
    NTSTATUS Status;

    PAGED_CODE();

    if (IoDescriptor->Type != CmResourceTypeMemory && IoDescriptor->Type != CmResourceTypeMemoryLarge)
        return STATUS_INVALID_PARAMETER;

    Length = RtlIoDecodeMemIoResource(IoDescriptor, NULL, NULL, NULL);
    RtlZeroMemory(CmDescriptor, sizeof(*CmDescriptor));
    Status = RtlCmEncodeMemIoResource(CmDescriptor, IoDescriptor->Type, Length, Start);
    if (!NT_SUCCESS(Status))
        return Status;

    CmDescriptor->ShareDisposition = IoDescriptor->ShareDisposition;
    CmDescriptor->Flags = (CmDescriptor->Flags & CM_RESOURCE_MEMORY_LARGE) |
                          (IoDescriptor->Flags & ~CM_RESOURCE_MEMORY_LARGE);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopArbMemUnpackResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PUINT64 Start,
    _Out_ PUINT64 OutLength)
{
    PAGED_CODE();

    if (CmDescriptor->Type != CmResourceTypeMemory && CmDescriptor->Type != CmResourceTypeMemoryLarge)
        return STATUS_INVALID_PARAMETER;

    *OutLength = RtlCmDecodeMemIoResource(CmDescriptor, Start);
    return STATUS_SUCCESS;
}

static
INT32
NTAPI
IopArbMemScoreRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    UINT64 Minimum, Maximum, Length, Alignment, Positions;

    PAGED_CODE();

    if (!NT_SUCCESS(IopArbMemUnpackRequirements(IoDescriptor, &Minimum, &Maximum, &Length, &Alignment)) ||
        Length == 0 || Maximum < Minimum || Maximum - Minimum < Length - 1)
    {
        return 0;
    }

    Positions = (Maximum - Minimum - (Length - 1)) / Alignment + 1;
    return Positions > MAXLONG ? MAXLONG : (INT32)Positions;
}

static
NTSTATUS
NTAPI
IopArbMemInitialize(VOID)
{
    NTSTATUS Status;

    PAGED_CODE();

    IopRootMemArbiter.UnpackRequirement = IopArbMemUnpackRequirements;
    IopRootMemArbiter.PackResource = IopArbMemPackResource;
    IopRootMemArbiter.UnpackResource = IopArbMemUnpackResource;
    IopRootMemArbiter.ScoreRequirement = IopArbMemScoreRequirement;

    Status = ArbInitializeArbiterInstance(&IopRootMemArbiter,
                                          NULL,
                                          CmResourceTypeMemory,
                                          L"RootMemory",
                                          L"Root",
                                          NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopArbMemInitialize: Failed with %X\n", Status);
    }

    return Status;
}

static
NTSTATUS
NTAPI
IopArbIrqUnpackRequirements(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PUINT64 OutMinimumAddress,
    _Out_ PUINT64 OutMaximumAddress,
    _Out_ PUINT64 OutLength,
    _Out_ PUINT64 OutAlignment)
{
    PAGED_CODE();

    if (IoDescriptor->Type != CmResourceTypeInterrupt)
        return STATUS_INVALID_PARAMETER;

    *OutMinimumAddress = IoDescriptor->u.Interrupt.MinimumVector;
    *OutMaximumAddress = IoDescriptor->u.Interrupt.MaximumVector;
    *OutLength = 1;
    *OutAlignment = 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopArbIrqPackResource(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ UINT64 Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    PAGED_CODE();

    if (IoDescriptor->Type != CmResourceTypeInterrupt)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(CmDescriptor, sizeof(*CmDescriptor));
    CmDescriptor->Type = CmResourceTypeInterrupt;
    CmDescriptor->ShareDisposition = IoDescriptor->ShareDisposition;
    CmDescriptor->Flags = IoDescriptor->Flags;
    CmDescriptor->u.Interrupt.Level = (ULONG)Start;
    CmDescriptor->u.Interrupt.Vector = (ULONG)Start;
    CmDescriptor->u.Interrupt.Affinity = IoDescriptor->u.Interrupt.TargetedProcessors
                                             ? IoDescriptor->u.Interrupt.TargetedProcessors
                                             : (KAFFINITY)-1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopArbIrqUnpackResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PUINT64 Start,
    _Out_ PUINT64 OutLength)
{
    PAGED_CODE();

    if (CmDescriptor->Type != CmResourceTypeInterrupt)
        return STATUS_INVALID_PARAMETER;

    *Start = CmDescriptor->u.Interrupt.Vector;
    *OutLength = 1;
    return STATUS_SUCCESS;
}

static
INT32
NTAPI
IopArbIrqScoreRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    UINT64 Minimum, Maximum, Length, Alignment, Positions;

    PAGED_CODE();

    if (!NT_SUCCESS(IopArbIrqUnpackRequirements(IoDescriptor, &Minimum, &Maximum, &Length, &Alignment)) ||
        Length == 0 || Maximum < Minimum || Maximum - Minimum < Length - 1)
    {
        return 0;
    }

    Positions = (Maximum - Minimum - (Length - 1)) / Alignment + 1;
    return Positions > MAXLONG ? MAXLONG : (INT32)Positions;
}

static
NTSTATUS
NTAPI
IopArbIrqInitialize(VOID)
{
    NTSTATUS Status;

    PAGED_CODE();

    IopRootIrqArbiter.UnpackRequirement = IopArbIrqUnpackRequirements;
    IopRootIrqArbiter.PackResource = IopArbIrqPackResource;
    IopRootIrqArbiter.UnpackResource = IopArbIrqUnpackResource;
    IopRootIrqArbiter.ScoreRequirement = IopArbIrqScoreRequirement;

    Status = ArbInitializeArbiterInstance(&IopRootIrqArbiter,
                                          NULL,
                                          CmResourceTypeInterrupt,
                                          L"RootIRQ",
                                          L"Root",
                                          NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopArbIrqInitialize: Failed with %X\n", Status);
    }

    return Status;
}

static
NTSTATUS
NTAPI
IopArbDmaUnpackRequirements(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PUINT64 OutMinimumAddress,
    _Out_ PUINT64 OutMaximumAddress,
    _Out_ PUINT64 OutLength,
    _Out_ PUINT64 OutAlignment)
{
    PAGED_CODE();

    if (IoDescriptor->Type != CmResourceTypeDma)
        return STATUS_INVALID_PARAMETER;

    *OutMinimumAddress = IoDescriptor->u.Dma.MinimumChannel;
    *OutMaximumAddress = IoDescriptor->u.Dma.MaximumChannel;
    *OutLength = 1;
    *OutAlignment = 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopArbDmaPackResource(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ UINT64 Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    PAGED_CODE();

    if (IoDescriptor->Type != CmResourceTypeDma)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(CmDescriptor, sizeof(*CmDescriptor));
    CmDescriptor->Type = CmResourceTypeDma;
    CmDescriptor->ShareDisposition = IoDescriptor->ShareDisposition;
    CmDescriptor->Flags = IoDescriptor->Flags;
    CmDescriptor->u.Dma.Channel = (ULONG)Start;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopArbDmaUnpackResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PUINT64 Start,
    _Out_ PUINT64 OutLength)
{
    PAGED_CODE();

    if (CmDescriptor->Type != CmResourceTypeDma)
        return STATUS_INVALID_PARAMETER;

    *Start = CmDescriptor->u.Dma.Channel;
    *OutLength = 1;
    return STATUS_SUCCESS;
}

static
INT32
NTAPI
IopArbDmaScoreRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    UINT64 Minimum, Maximum, Length, Alignment, Positions;

    PAGED_CODE();

    if (!NT_SUCCESS(IopArbDmaUnpackRequirements(IoDescriptor, &Minimum, &Maximum, &Length, &Alignment)) ||
        Length == 0 || Maximum < Minimum || Maximum - Minimum < Length - 1)
    {
        return 0;
    }

    Positions = (Maximum - Minimum - (Length - 1)) / Alignment + 1;
    return Positions > MAXLONG ? MAXLONG : (INT32)Positions;
}

static
NTSTATUS
NTAPI
IopArbDmaInitialize(VOID)
{
    NTSTATUS Status;

    PAGED_CODE();

    IopRootDmaArbiter.UnpackRequirement = IopArbDmaUnpackRequirements;
    IopRootDmaArbiter.PackResource = IopArbDmaPackResource;
    IopRootDmaArbiter.UnpackResource = IopArbDmaUnpackResource;
    IopRootDmaArbiter.ScoreRequirement = IopArbDmaScoreRequirement;

    Status = ArbInitializeArbiterInstance(&IopRootDmaArbiter,
                                          NULL,
                                          CmResourceTypeDma,
                                          L"RootDma",
                                          L"Root",
                                          NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopArbDmaInitialize: Failed with %X\n", Status);
    }

    return Status;
}

static
NTSTATUS
NTAPI
IopArbBusNumberUnpackRequirements(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PUINT64 OutMinimumAddress,
    _Out_ PUINT64 OutMaximumAddress,
    _Out_ PUINT64 OutLength,
    _Out_ PUINT64 OutAlignment)
{
    PAGED_CODE();

    if (IoDescriptor->Type != CmResourceTypeBusNumber)
        return STATUS_INVALID_PARAMETER;

    *OutMinimumAddress = IoDescriptor->u.BusNumber.MinBusNumber;
    *OutMaximumAddress = IoDescriptor->u.BusNumber.MaxBusNumber;
    *OutLength = IoDescriptor->u.BusNumber.Length;
    *OutAlignment = 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopArbBusNumberPackResource(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ UINT64 Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    PAGED_CODE();

    if (IoDescriptor->Type != CmResourceTypeBusNumber)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(CmDescriptor, sizeof(*CmDescriptor));
    CmDescriptor->Type = CmResourceTypeBusNumber;
    CmDescriptor->ShareDisposition = IoDescriptor->ShareDisposition;
    CmDescriptor->Flags = IoDescriptor->Flags;
    CmDescriptor->u.BusNumber.Start = (ULONG)Start;
    CmDescriptor->u.BusNumber.Length = IoDescriptor->u.BusNumber.Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopArbBusNumberUnpackResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PUINT64 Start,
    _Out_ PUINT64 OutLength)
{
    PAGED_CODE();

    if (CmDescriptor->Type != CmResourceTypeBusNumber)
        return STATUS_INVALID_PARAMETER;

    *Start = CmDescriptor->u.BusNumber.Start;
    *OutLength = CmDescriptor->u.BusNumber.Length;
    return STATUS_SUCCESS;
}

static
INT32
NTAPI
IopArbBusNumberScoreRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    UINT64 Minimum, Maximum, Length, Alignment, Positions;

    PAGED_CODE();

    if (!NT_SUCCESS(IopArbBusNumberUnpackRequirements(IoDescriptor, &Minimum, &Maximum, &Length, &Alignment)) ||
        Length == 0 || Maximum < Minimum || Maximum - Minimum < Length - 1)
    {
        return 0;
    }

    Positions = (Maximum - Minimum - (Length - 1)) / Alignment + 1;
    return Positions > MAXLONG ? MAXLONG : (INT32)Positions;
}

static
NTSTATUS
NTAPI
IopArbBusNumberInitialize(VOID)
{
    NTSTATUS Status;

    PAGED_CODE();

    IopRootBusNumberArbiter.UnpackRequirement = IopArbBusNumberUnpackRequirements;
    IopRootBusNumberArbiter.PackResource = IopArbBusNumberPackResource;
    IopRootBusNumberArbiter.UnpackResource = IopArbBusNumberUnpackResource;
    IopRootBusNumberArbiter.ScoreRequirement = IopArbBusNumberScoreRequirement;

    Status = ArbInitializeArbiterInstance(&IopRootBusNumberArbiter,
                                          NULL,
                                          CmResourceTypeBusNumber,
                                          L"RootBusNumber",
                                          L"Root",
                                          NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopArbBusNumberInitialize: Failed with %X\n", Status);
    }

    return Status;
}

NTSTATUS
NTAPI
IopInitializeArbiters(VOID)
{
    static NTSTATUS (NTAPI * const Initializers[])(VOID) =
    {
        IopArbPortInitialize,
        IopArbMemInitialize,
        IopArbDmaInitialize,
        IopArbIrqInitialize,
        IopArbBusNumberInitialize,
    };
    NTSTATUS Status;
    ULONG Index;

    PAGED_CODE();

    for (Index = 0; Index < RTL_NUMBER_OF(Initializers); Index++)
    {
        Status = Initializers[Index]();
        if (!NT_SUCCESS(Status))
            return Status;
    }
    return STATUS_SUCCESS;
}
