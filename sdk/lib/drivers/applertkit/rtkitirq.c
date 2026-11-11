/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interrupt-driven receive path of the Apple silicon coprocessor mailbox
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/applertkit.h>

#define RTKIT_IRQ_TAG 'iRpA'

struct _APPLE_RTKIT_INTERRUPT
{
    PAPPLE_RTKIT Rtkit;
    KDPC Dpc;
    PKEVENT Notify;
    volatile LONG FiredLogged;
};

static
BOOLEAN
NTAPI
RtkitInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID Context)
{
    PAPPLE_RTKIT_INTERRUPT State = Context;

    UNREFERENCED_PARAMETER(Interrupt);
    ASSERT(State && State->Rtkit->InterruptState == State);
    if (!AppleRtkitDrainMailbox(State->Rtkit))
        return FALSE;
    InterlockedIncrement(&State->Rtkit->Interrupts);
    KeInsertQueueDpc(&State->Dpc, NULL, NULL);
    return TRUE;
}

static
VOID
NTAPI
RtkitInterruptDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PAPPLE_RTKIT_INTERRUPT State = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    ASSERT(State && KeGetCurrentIrql() == DISPATCH_LEVEL);
    if (!InterlockedExchange(&State->FiredLogged, 1))
        DbgPrint("%s: mailbox interrupt fired\n", State->Rtkit->Name);
    if (State->Notify)
        KeSetEvent(State->Notify, IO_NO_INCREMENT, FALSE);
}

NTSTATUS
NTAPI
AppleRtkitConnectInterrupt(
    _Inout_ PAPPLE_RTKIT Rtkit,
    _In_opt_ PCM_RESOURCE_LIST Raw,
    _In_opt_ PCM_RESOURCE_LIST Translated,
    _In_ ULONG Gsi,
    _In_opt_ PKEVENT Notify)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR RawPartial, Partial;
    PAPPLE_RTKIT_INTERRUPT State;
    ULONG Index, Count;
    NTSTATUS Status = STATUS_NOT_FOUND;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Rtkit->Mailbox && !Rtkit->Interrupt && !Rtkit->InterruptState);
    if (!Raw || !Translated || !Raw->Count || !Translated->Count ||
        Raw->List[0].PartialResourceList.Count != Translated->List[0].PartialResourceList.Count)
    {
        DbgPrint("%s: no mailbox interrupt was assigned, polling\n", Rtkit->Name);
        return STATUS_NOT_FOUND;
    }
    Count = Raw->List[0].PartialResourceList.Count;
    for (Index = 0; Index < Count; ++Index)
    {
        RawPartial = &Raw->List[0].PartialResourceList.PartialDescriptors[Index];
        Partial = &Translated->List[0].PartialResourceList.PartialDescriptors[Index];
        if (RawPartial->Type == CmResourceTypeInterrupt && Partial->Type == CmResourceTypeInterrupt &&
            RawPartial->u.Interrupt.Vector == Gsi)
        {
            break;
        }
    }
    if (Index == Count)
    {
        DbgPrint("%s: mailbox interrupt %lu was not assigned, polling\n", Rtkit->Name, Gsi);
        return STATUS_NOT_FOUND;
    }

    State = ExAllocatePoolZero(NonPagedPool, sizeof(*State), RTKIT_IRQ_TAG);
    if (!State)
        return STATUS_INSUFFICIENT_RESOURCES;
    State->Rtkit = Rtkit;
    State->Notify = Notify;
    KeInitializeDpc(&State->Dpc, RtkitInterruptDpc, State);
    Rtkit->InterruptState = State;
    Status = IoConnectInterrupt(&Rtkit->Interrupt, RtkitInterruptService, State, NULL, Partial->u.Interrupt.Vector,
                                (KIRQL)Partial->u.Interrupt.Level, (KIRQL)Partial->u.Interrupt.Level,
                                (Partial->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive,
                                Partial->ShareDisposition == CmResourceShareShared, Partial->u.Interrupt.Affinity,
                                FALSE);
    DbgPrint("%s: mailbox interrupt %lu (vector %lu level %lu) connected: %lx\n", Rtkit->Name, Gsi,
             Partial->u.Interrupt.Vector, Partial->u.Interrupt.Level, Status);
    if (!NT_SUCCESS(Status))
    {
        Rtkit->Interrupt = NULL;
        Rtkit->InterruptState = NULL;
        ExFreePoolWithTag(State, RTKIT_IRQ_TAG);
    }
    return Status;
}

VOID
NTAPI
AppleRtkitDisconnectInterrupt(
    _Inout_ PAPPLE_RTKIT Rtkit)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(!Rtkit->Interrupt == !Rtkit->InterruptState);
    if (!Rtkit->Interrupt)
        return;
    IoDisconnectInterrupt(Rtkit->Interrupt);
    Rtkit->Interrupt = NULL;
    KeFlushQueuedDpcs();
    ExFreePoolWithTag(Rtkit->InterruptState, RTKIT_IRQ_TAG);
    Rtkit->InterruptState = NULL;
    DbgPrint("%s: mailbox interrupt disconnected after %ld interrupts\n", Rtkit->Name, Rtkit->Interrupts);
}

NTSTATUS
NTAPI
AppleAddInterruptRequirement(
    _Inout_ PIRP Irp,
    _In_ ULONG Gsi,
    _In_ BOOLEAN EdgeTriggered)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PIO_RESOURCE_REQUIREMENTS_LIST Old, New;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Count = 0, Index, Size;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(Stack->MajorFunction == IRP_MJ_PNP && Stack->MinorFunction == IRP_MN_FILTER_RESOURCE_REQUIREMENTS);
    Old = (PIO_RESOURCE_REQUIREMENTS_LIST)Irp->IoStatus.Information;
    if (!Old)
        Old = Stack->Parameters.FilterResourceRequirements.IoResourceRequirementList;
    if (Old)
    {
        if (Old->AlternativeLists != 1)
            return STATUS_NOT_SUPPORTED;
        Count = Old->List[0].Count;
        for (Index = 0; Index < Count; ++Index)
        {
            Descriptor = &Old->List[0].Descriptors[Index];
            if (Descriptor->Type == CmResourceTypeInterrupt && Descriptor->u.Interrupt.MinimumVector <= Gsi &&
                Descriptor->u.Interrupt.MaximumVector >= Gsi)
            {
                Irp->IoStatus.Information = (ULONG_PTR)Old;
                return STATUS_SUCCESS;
            }
        }
    }

    Size = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
           (Count + 1) * sizeof(IO_RESOURCE_DESCRIPTOR);
    New = ExAllocatePoolZero(PagedPool, Size, RTKIT_IRQ_TAG);
    if (!New)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (Old)
    {
        RtlCopyMemory(New, Old, FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
                                Count * sizeof(IO_RESOURCE_DESCRIPTOR));
    }
    else
    {
        New->InterfaceType = Internal;
        New->List[0].Version = 1;
        New->List[0].Revision = 1;
    }
    New->ListSize = Size;
    New->AlternativeLists = 1;
    New->List[0].Count = Count + 1;
    Descriptor = &New->List[0].Descriptors[Count];
    Descriptor->Type = CmResourceTypeInterrupt;
    Descriptor->ShareDisposition = CmResourceShareShared;
    Descriptor->Flags = EdgeTriggered ? CM_RESOURCE_INTERRUPT_LATCHED : CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
    Descriptor->u.Interrupt.MinimumVector = Gsi;
    Descriptor->u.Interrupt.MaximumVector = Gsi;
    if (Old)
        ExFreePool(Old);
    Irp->IoStatus.Information = (ULONG_PTR)New;
    return STATUS_SUCCESS;
}
