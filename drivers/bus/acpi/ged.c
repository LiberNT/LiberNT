/*
 * PROJECT:     LiberNT ACPI bus driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interrupt-signaled ACPI events of the Generic Event Device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <reactos/hal/msi.h>

#define NDEBUG
#include <debug.h>

#define TAG_GED 'deGA'

typedef struct _GED_EVENT
{
    ACPI_HANDLE Method;
    ULONG Gsiv;
    BOOLEAN PassGsiv;
    BOOLEAN LevelSensitive;
    ULONG Vector;
    KIRQL Irql;
    PKINTERRUPT Interrupt;
    KDPC Dpc;
    WORK_QUEUE_ITEM WorkItem;
    volatile LONG Pending;
} GED_EVENT, *PGED_EVENT;

static BOOLEAN GedInitialized;

VOID
BuspMaskInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql);

VOID
BuspUnmaskInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE Mode);

static
VOID
NTAPI
GedWorker(
    _In_ PVOID Context)
{
    PGED_EVENT Event = Context;
    ACPI_OBJECT Argument;
    ACPI_OBJECT_LIST Arguments;
    ACPI_STATUS Status;

    for (;;)
    {
        InterlockedExchange(&Event->Pending, 1);

        Argument.Type = ACPI_TYPE_INTEGER;
        Argument.Integer.Value = Event->Gsiv;
        Arguments.Count = 1;
        Arguments.Pointer = &Argument;
        Status = AcpiEvaluateObject(Event->Method, NULL, Event->PassGsiv ? &Arguments : NULL, NULL);
        if (ACPI_FAILURE(Status))
            DPRINT1("ACPI: generic event %lu was not handled: %s\n", Event->Gsiv, AcpiFormatException(Status));

        if (Event->LevelSensitive)
            BuspUnmaskInterrupt(Event->Vector, Event->Irql, LevelSensitive);

        if (InterlockedCompareExchange(&Event->Pending, 0, 1) == 1)
            break;
    }
}

static
VOID
NTAPI
GedDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PGED_EVENT Event = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    ExQueueWorkItem(&Event->WorkItem, CriticalWorkQueue);
}

static
BOOLEAN
NTAPI
GedInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID Context)
{
    PGED_EVENT Event = Context;

    UNREFERENCED_PARAMETER(Interrupt);

    if (Event->LevelSensitive)
        BuspMaskInterrupt(Event->Vector, Event->Irql);

    if (InterlockedIncrement(&Event->Pending) == 1)
        KeInsertQueueDpc(&Event->Dpc, NULL, NULL);

    return TRUE;
}

static
VOID
GedConnectEvent(
    _In_ ACPI_HANDLE Device,
    _In_ ULONG Gsiv,
    _In_ BOOLEAN Level,
    _In_ BOOLEAN Shared)
{
    IO_CONNECT_INTERRUPT_PARAMETERS Parameters;
    HAL_INTERRUPT_TARGET_INFORMATION TargetInfo;
    ACPI_HANDLE Method = NULL;
    BOOLEAN PassGsiv = FALSE;
    KAFFINITY Affinity;
    PGED_EVENT Event;
    NTSTATUS Status;
    char Name[5];
    KIRQL Irql;

    if (Gsiv <= 255)
    {
        Name[0] = '_';
        Name[1] = Level ? 'L' : 'E';
        Name[2] = "0123456789ABCDEF"[(Gsiv >> 4) & 0xF];
        Name[3] = "0123456789ABCDEF"[Gsiv & 0xF];
        Name[4] = ANSI_NULL;
        if (ACPI_FAILURE(AcpiGetHandle(Device, Name, &Method)))
            Method = NULL;
    }

    if (!Method)
    {
        if (ACPI_FAILURE(AcpiGetHandle(Device, METHOD_NAME__EVT, &Method)))
        {
            DPRINT1("ACPI: generic event interrupt %lu has no event method\n", Gsiv);
            return;
        }
        PassGsiv = TRUE;
    }

    Event = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Event), TAG_GED);
    if (!Event)
        return;

    Event->Method = Method;
    Event->Gsiv = Gsiv;
    Event->PassGsiv = PassGsiv;
    Event->LevelSensitive = Level;
    KeInitializeDpc(&Event->Dpc, GedDpc, Event);
    ExInitializeWorkItem(&Event->WorkItem, GedWorker, Event);

    Event->Vector = HalGetInterruptVector(Internal, 0, Gsiv, Gsiv, &Irql, &Affinity);
    Event->Irql = Irql;

    RtlZeroMemory(&TargetInfo, sizeof(TargetInfo));
    TargetInfo.Version = HAL_INTERRUPT_TARGET_INFORMATION_VERSION;
    TargetInfo.TargetProcessors = Affinity;
    HalGetInterruptTargetInformation(&TargetInfo);

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.Version = CONNECT_FULLY_SPECIFIED;
    Parameters.FullySpecified.InterruptObject = &Event->Interrupt;
    Parameters.FullySpecified.ServiceRoutine = GedInterruptService;
    Parameters.FullySpecified.ServiceContext = Event;
    Parameters.FullySpecified.Vector = Event->Vector;
    Parameters.FullySpecified.Irql = Event->Irql;
    Parameters.FullySpecified.SynchronizeIrql = Event->Irql;
    Parameters.FullySpecified.InterruptMode = Level ? LevelSensitive : Latched;
    Parameters.FullySpecified.ShareVector = Shared;
    Parameters.FullySpecified.ProcessorEnableMask =
        TargetInfo.TargetProcessors ? TargetInfo.TargetProcessors : Affinity;

    Status = IoConnectInterruptEx(&Parameters);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPI: generic event interrupt %lu could not be connected (0x%08lx)\n", Gsiv, Status);
        ExFreePoolWithTag(Event, TAG_GED);
        return;
    }

    DPRINT1("ACPI: generic event interrupt %lu connected to %s\n", Gsiv, PassGsiv ? "_EVT" : Name);
}

static
ACPI_STATUS
GedResourceCallback(
    ACPI_RESOURCE *Resource,
    void *Context)
{
    ACPI_HANDLE Device = Context;
    UINT32 Index;

    switch (Resource->Type)
    {
        case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
            if (Resource->Data.ExtendedIrq.ProducerConsumer != ACPI_CONSUMER)
                break;
            for (Index = 0; Index < Resource->Data.ExtendedIrq.InterruptCount; Index++)
            {
                GedConnectEvent(Device,
                                Resource->Data.ExtendedIrq.Interrupts[Index],
                                Resource->Data.ExtendedIrq.Triggering == ACPI_LEVEL_SENSITIVE,
                                Resource->Data.ExtendedIrq.Shareable == ACPI_SHARED);
            }
            break;

        case ACPI_RESOURCE_TYPE_IRQ:
            for (Index = 0; Index < Resource->Data.Irq.InterruptCount; Index++)
            {
                GedConnectEvent(Device,
                                Resource->Data.Irq.Interrupts[Index],
                                Resource->Data.Irq.Triggering == ACPI_LEVEL_SENSITIVE,
                                Resource->Data.Irq.Shareable == ACPI_SHARED);
            }
            break;

        default:
            break;
    }

    return AE_OK;
}

static
ACPI_STATUS
GedAddDevice(
    ACPI_HANDLE Device,
    UINT32 NestingLevel,
    void *Context,
    void **ReturnValue)
{
    ACPI_STATUS Status;

    UNREFERENCED_PARAMETER(NestingLevel);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ReturnValue);

    Status = AcpiWalkResources(Device, METHOD_NAME__CRS, GedResourceCallback, Device);
    if (ACPI_FAILURE(Status))
        DPRINT1("ACPI: generic event device resources could not be read: %s\n", AcpiFormatException(Status));

    return AE_OK;
}

int
acpi_ged_init(void)
{
    if (GedInitialized)
        return 0;

    GedInitialized = TRUE;
    AcpiGetDevices("ACPI0013", GedAddDevice, NULL, NULL);
    return 0;
}
