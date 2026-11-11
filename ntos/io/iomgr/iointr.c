/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Connecting a driver's service routine to an interrupt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "../ioint.h"
#include "../pnpio.h"
#include <reactos/hal/msi.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
IopFindLineBasedInterrupt(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG Vector,
    _Out_ PKIRQL Irql,
    _Out_ PKAFFINITY Affinity,
    _Out_ KINTERRUPT_MODE *Mode,
    _Out_ PBOOLEAN ShareVector)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_RESOURCE_LIST Resources;
    PDEVICE_NODE DeviceNode;
    ULONG Index;

    DeviceNode = IopGetDeviceNode(PhysicalDeviceObject);
    if (DeviceNode == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    Resources = DeviceNode->ResourceListTranslated;
    if (Resources == NULL || Resources->Count == 0)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Descriptor = Resources->List[0].PartialResourceList.PartialDescriptors;

    for (Index = 0;
         Index < Resources->List[0].PartialResourceList.Count;
         Index++)
    {
        if (Descriptor->Type == CmResourceTypeInterrupt)
        {
            *Vector = Descriptor->u.Interrupt.Vector;
            *Irql = (KIRQL)Descriptor->u.Interrupt.Level;
            *Affinity = Descriptor->u.Interrupt.Affinity;
            *Mode = (Descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED)
                        ? Latched
                        : LevelSensitive;
            *ShareVector =
                (Descriptor->ShareDisposition == CmResourceShareShared);

            return STATUS_SUCCESS;
        }

        Descriptor = PipGetNextCmPartialDescriptor(Descriptor);
    }

    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS
IopConnectInterruptExFullySpecific(
    _Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters)
{
    return IoConnectInterrupt(Parameters->FullySpecified.InterruptObject,
                              Parameters->FullySpecified.ServiceRoutine,
                              Parameters->FullySpecified.ServiceContext,
                              Parameters->FullySpecified.SpinLock,
                              Parameters->FullySpecified.Vector,
                              Parameters->FullySpecified.Irql,
                              Parameters->FullySpecified.SynchronizeIrql,
                              Parameters->FullySpecified.InterruptMode,
                              Parameters->FullySpecified.ShareVector,
                              Parameters->FullySpecified.ProcessorEnableMask,
                              Parameters->FullySpecified.FloatingSave);
}

NTSTATUS
IopConnectInterruptExLineBased(
    _Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters)
{
    KINTERRUPT_MODE Mode = LevelSensitive;
    BOOLEAN ShareVector = FALSE;
    KAFFINITY Affinity = 0;
    KIRQL Irql = PASSIVE_LEVEL;
    ULONG Vector = 0;
    NTSTATUS Status;
    KIRQL Synchronize;

    Status = IopFindLineBasedInterrupt(
        Parameters->LineBased.PhysicalDeviceObject,
        &Vector,
        &Irql,
        &Affinity,
        &Mode,
        &ShareVector);
    if (!NT_SUCCESS(Status)) return Status;

    Synchronize = Parameters->LineBased.SynchronizeIrql;
    if (Synchronize < Irql) Synchronize = Irql;

    return IoConnectInterrupt(Parameters->LineBased.InterruptObject,
                              Parameters->LineBased.ServiceRoutine,
                              Parameters->LineBased.ServiceContext,
                              Parameters->LineBased.SpinLock,
                              Vector,
                              Irql,
                              Synchronize,
                              Mode,
                              ShareVector,
                              Affinity,
                              Parameters->LineBased.FloatingSave);
}

static
BOOLEAN
NTAPI
IopServiceInterruptMessage(
    _In_ PKINTERRUPT Interrupt,
    _In_opt_ PVOID ServiceContext)
{
    PKMESSAGE_SERVICE_ROUTINE MessageServiceRoutine;

    MessageServiceRoutine =
        (PKMESSAGE_SERVICE_ROUTINE)Interrupt->MessageServiceRoutine;

    return MessageServiceRoutine(Interrupt,
                                 ServiceContext,
                                 Interrupt->MessageIndex);
}

static
NTSTATUS
IopConnectInterruptObjects(
    _Out_ PKINTERRUPT *InterruptObject,
    _In_ PKSERVICE_ROUTINE ServiceRoutine,
    _In_opt_ PKMESSAGE_SERVICE_ROUTINE MessageServiceRoutine,
    _In_ ULONG MessageIndex,
    _In_opt_ PVOID ServiceContext,
    _In_opt_ PKSPIN_LOCK SpinLock,
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KIRQL SynchronizeIrql,
    _In_ KINTERRUPT_MODE InterruptMode,
    _In_ BOOLEAN ShareVector,
    _In_ KAFFINITY ProcessorEnableMask,
    _In_ BOOLEAN FloatingSave)
{
    PIO_INTERRUPT IoInterrupt;
    PKSPIN_LOCK Lock;
    KAFFINITY Affinity;
    ULONG Processor;
    ULONG Count = 0;

    Affinity = ProcessorEnableMask & KeQueryActiveProcessors();
    if (Affinity == 0) return STATUS_INVALID_PARAMETER;

    IoInterrupt = ExAllocatePoolZero(NonPagedPool,
                                     sizeof(IO_INTERRUPT),
                                     TAG_IO_INTERRUPT);
    if (IoInterrupt == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    if (SpinLock != NULL)
    {
        Lock = SpinLock;
    }
    else
    {
        Lock = &IoInterrupt->SpinLock;
        KeInitializeSpinLock(Lock);
    }

    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        PKINTERRUPT Interrupt;

        if (!(Affinity & AFFINITY_MASK(Processor))) continue;

        if (Count == 0)
        {
            Interrupt = &IoInterrupt->FirstInterrupt;
        }
        else
        {
            Interrupt = ExAllocatePoolZero(NonPagedPool,
                                           sizeof(KINTERRUPT),
                                           TAG_IO_INTERRUPT);
            if (Interrupt == NULL)
            {
                IoDisconnectInterrupt(&IoInterrupt->FirstInterrupt);
                return STATUS_INSUFFICIENT_RESOURCES;
            }
        }

        KeInitializeInterrupt(Interrupt,
                              ServiceRoutine,
                              ServiceContext,
                              Lock,
                              Vector,
                              Irql,
                              SynchronizeIrql,
                              InterruptMode,
                              ShareVector,
                              (CCHAR)Processor,
                              FloatingSave);

        if (MessageServiceRoutine != NULL)
        {
            Interrupt->MessageServiceRoutine =
                (PKSERVICE_ROUTINE)MessageServiceRoutine;
            Interrupt->MessageIndex = MessageIndex;
        }

        if (!KeConnectInterrupt(Interrupt))
        {
            if (Count != 0) ExFreePoolWithTag(Interrupt, TAG_IO_INTERRUPT);
            IoDisconnectInterrupt(&IoInterrupt->FirstInterrupt);
            return STATUS_INVALID_PARAMETER;
        }

        IoInterrupt->Interrupt[Processor] = Interrupt;
        Count++;
    }

    *InterruptObject = &IoInterrupt->FirstInterrupt;

    return STATUS_SUCCESS;
}

static
VOID
IopDisconnectInterruptMessages(
    _In_ PIO_INTERRUPT_MESSAGE_INFO MessageTable)
{
    ULONG Index;

    for (Index = 0; Index < MessageTable->MessageCount; Index++)
    {
        PKINTERRUPT Interrupt = MessageTable->MessageInfo[Index].InterruptObject;

        if (Interrupt == NULL) continue;

        IoDisconnectInterrupt(Interrupt);
    }

    ExFreePoolWithTag(MessageTable, TAG_IO_INTERRUPT);
}

/* Only the raw descriptor carries the message count, the translated one the vector and level */
static
ULONG
IopCountInterruptMessages(
    _In_ PCM_RESOURCE_LIST Raw,
    _In_ PCM_RESOURCE_LIST Translated,
    _Inout_ PKIRQL UnifiedIrql)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR RawDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG MessageCount = 0;
    ULONG Index;

    RawDescriptor = Raw->List[0].PartialResourceList.PartialDescriptors;
    Descriptor = Translated->List[0].PartialResourceList.PartialDescriptors;

    for (Index = 0;
         Index < Translated->List[0].PartialResourceList.Count;
         Index++)
    {
        if (Descriptor->Type == CmResourceTypeInterrupt &&
            (Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
        {
            MessageCount += RawDescriptor->u.MessageInterrupt.Raw.MessageCount;

            if (*UnifiedIrql < (KIRQL)Descriptor->u.MessageInterrupt.Translated.Level)
            {
                *UnifiedIrql = (KIRQL)Descriptor->u.MessageInterrupt.Translated.Level;
            }
        }

        RawDescriptor = PipGetNextCmPartialDescriptor(RawDescriptor);
        Descriptor = PipGetNextCmPartialDescriptor(Descriptor);
    }

    return MessageCount;
}

static
NTSTATUS
IopConnectInterruptMessage(
    _In_ PIO_CONNECT_INTERRUPT_MESSAGE_BASED_PARAMETERS Parameters,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _In_ ULONG Message,
    _In_ ULONG MessageIndex,
    _In_ KIRQL UnifiedIrql,
    _Out_ PIO_INTERRUPT_MESSAGE_INFO_ENTRY Entry)
{
    HAL_MESSAGE_ROUTING_INFO RoutingInfo;
    KINTERRUPT_MODE Mode;
    KIRQL Synchronize;
    NTSTATUS Status;

    Entry->Vector = Descriptor->u.MessageInterrupt.Translated.Vector + Message;
    Entry->Irql = (KIRQL)Descriptor->u.MessageInterrupt.Translated.Level;
    Entry->TargetProcessorSet = Descriptor->u.MessageInterrupt.Translated.Affinity;

    Mode = (Descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched
                                                               : LevelSensitive;
    Entry->Mode = Mode;
    Entry->Polarity = InterruptPolarityUnknown;

    RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
    RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
    RoutingInfo.MessageCount = 1;
    RoutingInfo.Vector = Entry->Vector;
    RoutingInfo.Irql = Entry->Irql;
    RoutingInfo.TargetProcessors = Entry->TargetProcessorSet;

    Status = HalpGetMessageRoutingInfo(&RoutingInfo);
    if (!NT_SUCCESS(Status)) return Status;

    Entry->MessageAddress = RoutingInfo.MessageAddress;
    Entry->MessageData = RoutingInfo.MessageData;

    if (Parameters->SpinLock != NULL)
    {
        Synchronize = UnifiedIrql;
    }
    else
    {
        Synchronize = Parameters->SynchronizeIrql;
        if (Synchronize < Entry->Irql) Synchronize = Entry->Irql;
    }

    return IopConnectInterruptObjects(&Entry->InterruptObject,
                                      IopServiceInterruptMessage,
                                      Parameters->MessageServiceRoutine,
                                      MessageIndex,
                                      Parameters->ServiceContext,
                                      Parameters->SpinLock,
                                      Entry->Vector,
                                      Entry->Irql,
                                      Synchronize,
                                      Mode,
                                      (Descriptor->ShareDisposition ==
                                       CmResourceShareShared),
                                      Entry->TargetProcessorSet,
                                      Parameters->FloatingSave);
}

static
NTSTATUS
IopConnectInterruptMessages(
    _In_ PIO_CONNECT_INTERRUPT_MESSAGE_BASED_PARAMETERS Parameters,
    _In_ PCM_RESOURCE_LIST Raw,
    _In_ PCM_RESOURCE_LIST Translated,
    _In_ ULONG MessageCount,
    _In_ KIRQL UnifiedIrql)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR RawDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PIO_INTERRUPT_MESSAGE_INFO MessageTable;
    ULONG MessageIndex = 0;
    ULONG Message;
    ULONG Index;
    NTSTATUS Status;

    MessageTable = ExAllocatePoolZero(NonPagedPool,
                                      FIELD_OFFSET(IO_INTERRUPT_MESSAGE_INFO,
                                                   MessageInfo[MessageCount]),
                                      TAG_IO_INTERRUPT);
    if (MessageTable == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    MessageTable->UnifiedIrql = UnifiedIrql;
    MessageTable->MessageCount = MessageCount;

    RawDescriptor = Raw->List[0].PartialResourceList.PartialDescriptors;
    Descriptor = Translated->List[0].PartialResourceList.PartialDescriptors;

    for (Index = 0;
         Index < Translated->List[0].PartialResourceList.Count;
         Index++)
    {
        if (Descriptor->Type == CmResourceTypeInterrupt &&
            (Descriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
        {
            for (Message = 0;
                 Message < RawDescriptor->u.MessageInterrupt.Raw.MessageCount;
                 Message++)
            {
                Status = IopConnectInterruptMessage(
                    Parameters,
                    Descriptor,
                    Message,
                    MessageIndex,
                    UnifiedIrql,
                    &MessageTable->MessageInfo[MessageIndex]);
                if (!NT_SUCCESS(Status))
                {
                    IopDisconnectInterruptMessages(MessageTable);
                    return Status;
                }

                MessageIndex++;
            }
        }

        RawDescriptor = PipGetNextCmPartialDescriptor(RawDescriptor);
        Descriptor = PipGetNextCmPartialDescriptor(Descriptor);
    }

    *Parameters->ConnectionContext.InterruptMessageTable = MessageTable;

    return STATUS_SUCCESS;
}

NTSTATUS
IopConnectInterruptExMessageBased(
    _Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters)
{
    IO_CONNECT_INTERRUPT_PARAMETERS LineBased;
    PCM_RESOURCE_LIST Translated;
    PDEVICE_NODE DeviceNode;
    PCM_RESOURCE_LIST Raw;
    ULONG MessageCount = 0;
    KIRQL UnifiedIrql;
    NTSTATUS Status;

    DeviceNode = IopGetDeviceNode(Parameters->MessageBased.PhysicalDeviceObject);
    if (DeviceNode == NULL) return STATUS_INVALID_DEVICE_REQUEST;

    Raw = DeviceNode->ResourceList;
    Translated = DeviceNode->ResourceListTranslated;
    UnifiedIrql = Parameters->MessageBased.SynchronizeIrql;

    if (Raw != NULL && Translated != NULL &&
        Raw->Count != 0 && Translated->Count != 0 &&
        Raw->List[0].PartialResourceList.Count ==
        Translated->List[0].PartialResourceList.Count)
    {
        MessageCount = IopCountInterruptMessages(Raw, Translated, &UnifiedIrql);
    }

    if (MessageCount != 0)
    {
        return IopConnectInterruptMessages(&Parameters->MessageBased,
                                           Raw,
                                           Translated,
                                           MessageCount,
                                           UnifiedIrql);
    }

    if (Parameters->MessageBased.FallBackServiceRoutine == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&LineBased, sizeof(LineBased));

    LineBased.Version = CONNECT_LINE_BASED;
    LineBased.LineBased.PhysicalDeviceObject =
        Parameters->MessageBased.PhysicalDeviceObject;
    LineBased.LineBased.InterruptObject =
        Parameters->MessageBased.ConnectionContext.InterruptObject;
    LineBased.LineBased.ServiceRoutine =
        Parameters->MessageBased.FallBackServiceRoutine;
    LineBased.LineBased.ServiceContext =
        Parameters->MessageBased.ServiceContext;
    LineBased.LineBased.SpinLock = Parameters->MessageBased.SpinLock;
    LineBased.LineBased.SynchronizeIrql =
        Parameters->MessageBased.SynchronizeIrql;
    LineBased.LineBased.FloatingSave =
        Parameters->MessageBased.FloatingSave;

    Status = IopConnectInterruptExLineBased(&LineBased);
    if (!NT_SUCCESS(Status)) return Status;

    Parameters->Version = CONNECT_LINE_BASED;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
IoConnectInterrupt(
    _Out_ PKINTERRUPT *InterruptObject,
    _In_ PKSERVICE_ROUTINE ServiceRoutine,
    _In_opt_ PVOID ServiceContext,
    _In_opt_ PKSPIN_LOCK SpinLock,
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KIRQL SynchronizeIrql,
    _In_ KINTERRUPT_MODE InterruptMode,
    _In_ BOOLEAN ShareVector,
    _In_ KAFFINITY ProcessorEnableMask,
    _In_ BOOLEAN FloatingSave)
{
    PAGED_CODE();

    return IopConnectInterruptObjects(InterruptObject,
                                      ServiceRoutine,
                                      NULL,
                                      0,
                                      ServiceContext,
                                      SpinLock,
                                      Vector,
                                      Irql,
                                      SynchronizeIrql,
                                      InterruptMode,
                                      ShareVector,
                                      ProcessorEnableMask,
                                      FloatingSave);
}

VOID
NTAPI
IoDisconnectInterrupt(
    _In_ PKINTERRUPT InterruptObject)
{
    PIO_INTERRUPT IoInterrupt;
    ULONG Processor;

    PAGED_CODE();

    IoInterrupt = CONTAINING_RECORD(InterruptObject,
                                    IO_INTERRUPT,
                                    FirstInterrupt);

    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        PKINTERRUPT Interrupt = IoInterrupt->Interrupt[Processor];

        if (Interrupt == NULL) continue;

        KeDisconnectInterrupt(Interrupt);

        if (Interrupt != &IoInterrupt->FirstInterrupt)
        {
            ExFreePoolWithTag(Interrupt, TAG_IO_INTERRUPT);
        }

        IoInterrupt->Interrupt[Processor] = NULL;
    }

    ExFreePoolWithTag(IoInterrupt, TAG_IO_INTERRUPT);
}

NTSTATUS
NTAPI
IoConnectInterruptEx(
    _Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters)
{
    PAGED_CODE();

    switch (Parameters->Version)
    {
        case CONNECT_FULLY_SPECIFIED:
        case CONNECT_FULLY_SPECIFIED_GROUP:
            return IopConnectInterruptExFullySpecific(Parameters);

        case CONNECT_LINE_BASED:
            return IopConnectInterruptExLineBased(Parameters);

        case CONNECT_MESSAGE_BASED:
            return IopConnectInterruptExMessageBased(Parameters);

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

VOID
NTAPI
IoDisconnectInterruptEx(
    _In_ PIO_DISCONNECT_INTERRUPT_PARAMETERS Parameters)
{
    PAGED_CODE();

    if (Parameters->ConnectionContext.Generic == NULL) return;

    switch (Parameters->Version)
    {
        case CONNECT_FULLY_SPECIFIED:
        case CONNECT_FULLY_SPECIFIED_GROUP:
        case CONNECT_LINE_BASED:
            IoDisconnectInterrupt(Parameters->ConnectionContext.InterruptObject);
            break;

        case CONNECT_MESSAGE_BASED:
            IopDisconnectInterruptMessages(
                Parameters->ConnectionContext.InterruptMessageTable);
            break;

        default:
            break;
    }
}

NTSTATUS
NTAPI
IoGetAffinityInterrupt(
    _In_ PKINTERRUPT InterruptObject,
    _Out_ PGROUP_AFFINITY GroupAffinity)
{
    PIO_INTERRUPT IoInterrupt;
    ULONG Processor;

    if (InterruptObject == NULL || GroupAffinity == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    IoInterrupt = CONTAINING_RECORD(InterruptObject,
                                    IO_INTERRUPT,
                                    FirstInterrupt);

    GroupAffinity->Group = 0;
    GroupAffinity->Mask = 0;

    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        if (IoInterrupt->Interrupt[Processor] == NULL) continue;

        GroupAffinity->Mask |= AFFINITY_MASK(Processor);
    }

    if (GroupAffinity->Mask == 0) return STATUS_NOT_FOUND;

    return STATUS_SUCCESS;
}

VOID
NTAPI
IoReportInterruptActive(
    _In_ PIO_REPORT_INTERRUPT_ACTIVE_STATE_PARAMETERS Parameters)
{
    /* TODO: no interrupt steering state is kept, so nothing is reported */
    UNREFERENCED_PARAMETER(Parameters);
}

VOID
NTAPI
IoReportInterruptInactive(
    _In_ PIO_REPORT_INTERRUPT_ACTIVE_STATE_PARAMETERS Parameters)
{
    /* TODO: no interrupt steering state is kept, so nothing is reported */
    UNREFERENCED_PARAMETER(Parameters);
}
