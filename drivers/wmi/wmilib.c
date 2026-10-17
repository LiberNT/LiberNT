/*
 * PROJECT:         ReactOS WMI driver
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * FILE:            drivers/wmi/wmilib.c
 * PURPOSE:         Windows Management Instrumentation
 * PROGRAMMERS:     Aleksey Bragin (aleksey@reactos.org)
 *
 */

/* INCLUDES *****************************************************************/

#include <stdio.h>
#include <ntddk.h>
#include <wmilib.h>
#include <wmistr.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

#define TAG_WMILIB 'bilW'

static
BOOLEAN
WmiLibFindGuid(
    _In_ PWMILIB_CONTEXT WmiLibInfo,
    _In_ LPCGUID Guid,
    _Out_ PULONG GuidIndex)
{
    ULONG Index;

    for (Index = 0; Index < WmiLibInfo->GuidCount; Index++)
    {
        if (WmiLibInfo->GuidList[Index].Guid &&
            IsEqualGUID(WmiLibInfo->GuidList[Index].Guid, Guid))
        {
            *GuidIndex = Index;
            return TRUE;
        }
    }

    return FALSE;
}

static
VOID
WmiLibWriteCountedString(
    _In_ PUCHAR Buffer,
    _In_ ULONG Offset,
    _In_ PCUNICODE_STRING String)
{
    USHORT Length = String->Length;

    RtlCopyMemory(Buffer + Offset, &Length, sizeof(USHORT));
    if (Length)
        RtlCopyMemory(Buffer + Offset + sizeof(USHORT), String->Buffer, Length);
}

static
NTSTATUS
WmiLibBuildRegInfo(
    _In_ PWMILIB_CONTEXT WmiLibInfo,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _In_ ULONG RegFlags,
    _In_ PCUNICODE_STRING InstanceName,
    _In_opt_ PCUNICODE_STRING RegistryPath,
    _In_ PCUNICODE_STRING MofResourceName,
    _In_opt_ PDEVICE_OBJECT Pdo)
{
    PWMIREGINFOW RegInfo;
    PWMIREGGUIDW RegGuid;
    ULONG Index, Needed, NameOffset = 0, RegistryOffset = 0, MofOffset = 0;
    BOOLEAN Update = (Stack->Parameters.WMI.DataPath == (PVOID)(ULONG_PTR)WMIUPDATE);
    BOOLEAN Extended = (Stack->MinorFunction == IRP_MN_REGINFO_EX);
    PUCHAR Buffer = Stack->Parameters.WMI.Buffer;
    ULONG BufferSize = Stack->Parameters.WMI.BufferSize;

    if ((RegFlags & WMIREG_FLAG_INSTANCE_PDO) && !Pdo)
        return STATUS_INVALID_DEVICE_REQUEST;

    Needed = FIELD_OFFSET(WMIREGINFOW, WmiRegGuid[WmiLibInfo->GuidCount]);
    if (RegFlags & WMIREG_FLAG_INSTANCE_BASENAME)
    {
        NameOffset = Needed;
        Needed += sizeof(USHORT) + InstanceName->Length;
        Needed = ALIGN_UP_BY(Needed, sizeof(USHORT));
    }
    if (!Update && RegistryPath && RegistryPath->Length)
    {
        RegistryOffset = Needed;
        Needed += sizeof(USHORT) + RegistryPath->Length;
        Needed = ALIGN_UP_BY(Needed, sizeof(USHORT));
    }
    if (!Update && MofResourceName->Length)
    {
        MofOffset = Needed;
        Needed += sizeof(USHORT) + MofResourceName->Length;
        Needed = ALIGN_UP_BY(Needed, sizeof(USHORT));
    }

    if (BufferSize < Needed)
    {
        if (BufferSize >= sizeof(ULONG))
        {
            *(PULONG)Buffer = Needed;
            Irp->IoStatus.Information = sizeof(ULONG);
        }
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(Buffer, Needed);
    RegInfo = (PWMIREGINFOW)Buffer;
    RegInfo->BufferSize = Needed;
    RegInfo->RegistryPath = RegistryOffset;
    RegInfo->MofResourceName = MofOffset;
    RegInfo->GuidCount = WmiLibInfo->GuidCount;

    for (Index = 0; Index < WmiLibInfo->GuidCount; Index++)
    {
        RegGuid = &RegInfo->WmiRegGuid[Index];
        RegGuid->Guid = *WmiLibInfo->GuidList[Index].Guid;
        RegGuid->Flags = WmiLibInfo->GuidList[Index].Flags | RegFlags;
        RegGuid->InstanceCount = WmiLibInfo->GuidList[Index].InstanceCount;

        if (RegGuid->Flags & WMIREG_FLAG_INSTANCE_PDO)
        {
            RegGuid->Pdo = (ULONG_PTR)Pdo;
            if (Extended)
                ObReferenceObject(Pdo);
        }
        else if (RegGuid->Flags & WMIREG_FLAG_INSTANCE_BASENAME)
        {
            RegGuid->BaseNameOffset = NameOffset;
        }
    }

    if (NameOffset)
        WmiLibWriteCountedString(Buffer, NameOffset, InstanceName);
    if (RegistryOffset)
        WmiLibWriteCountedString(Buffer, RegistryOffset, RegistryPath);
    if (MofOffset)
        WmiLibWriteCountedString(Buffer, MofOffset, MofResourceName);

    Irp->IoStatus.Information = Needed;
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmiLibRegInfo(
    _In_ PWMILIB_CONTEXT WmiLibInfo,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    UNICODE_STRING InstanceName = { 0 }, MofResourceName = { 0 };
    PUNICODE_STRING RegistryPath = NULL;
    PDEVICE_OBJECT Pdo = NULL;
    ULONG RegFlags = 0;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;

    if (!WmiLibInfo->QueryWmiRegInfo)
        return STATUS_INVALID_DEVICE_REQUEST;

    Status = WmiLibInfo->QueryWmiRegInfo(DeviceObject,
                                         &RegFlags,
                                         &InstanceName,
                                         &RegistryPath,
                                         &MofResourceName,
                                         &Pdo);
    if (NT_SUCCESS(Status))
    {
        Status = WmiLibBuildRegInfo(WmiLibInfo, Irp, Stack, RegFlags, &InstanceName,
                                    RegistryPath, &MofResourceName, Pdo);
    }

    if (InstanceName.Buffer)
        ExFreePool(InstanceName.Buffer);
    return Status;
}

static
BOOLEAN
WmiLibStaticInstance(
    _In_ PWMILIB_CONTEXT WmiLibInfo,
    _In_ ULONG GuidIndex,
    _In_ PWNODE_HEADER Header,
    _In_ ULONG InstanceIndex)
{
    if (!(Header->Flags & WNODE_FLAG_STATIC_INSTANCE_NAMES))
        return FALSE;

    return InstanceIndex < WmiLibInfo->GuidList[GuidIndex].InstanceCount;
}

NTSTATUS
NTAPI
WmiCompleteRequest(IN PDEVICE_OBJECT DeviceObject,
                   IN PIRP Irp,
                   IN NTSTATUS Status,
                   IN ULONG BufferUsed,
                   IN CCHAR PriorityBoost)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PUCHAR Buffer = Stack->Parameters.WMI.Buffer;
    ULONG BufferSize = Stack->Parameters.WMI.BufferSize;
    PWNODE_HEADER Header = (PWNODE_HEADER)Buffer;
    ULONG DataOffset = 0, Index, Count, Offset, Length;
    ULONG_PTR Information = 0;
    PWNODE_ALL_DATA AllData;
    PWNODE_SINGLE_INSTANCE Instance;
    PWNODE_METHOD_ITEM Method;
    PULONG Lengths;

    UNREFERENCED_PARAMETER(DeviceObject);

    switch (Stack->MinorFunction)
    {
        case IRP_MN_QUERY_ALL_DATA:
            AllData = (PWNODE_ALL_DATA)Buffer;
            DataOffset = AllData->DataBlockOffset;
            break;
        case IRP_MN_QUERY_SINGLE_INSTANCE:
            DataOffset = ((PWNODE_SINGLE_INSTANCE)Buffer)->DataBlockOffset;
            break;
        case IRP_MN_EXECUTE_METHOD:
            DataOffset = ((PWNODE_METHOD_ITEM)Buffer)->DataBlockOffset;
            break;
        default:
            break;
    }

    if (Status == STATUS_BUFFER_TOO_SMALL && DataOffset)
    {
        if (BufferSize >= sizeof(WNODE_TOO_SMALL))
        {
            Header->BufferSize = sizeof(WNODE_TOO_SMALL);
            Header->Flags |= WNODE_FLAG_TOO_SMALL;
            ((PWNODE_TOO_SMALL)Buffer)->SizeNeeded = DataOffset + BufferUsed;
            Information = sizeof(WNODE_TOO_SMALL);
            Status = STATUS_SUCCESS;
        }
    }
    else if (NT_SUCCESS(Status))
    {
        switch (Stack->MinorFunction)
        {
            case IRP_MN_QUERY_ALL_DATA:
            {
                AllData = (PWNODE_ALL_DATA)Buffer;
                Count = AllData->InstanceCount;
                Lengths = (PULONG)&AllData->OffsetInstanceDataAndLength[Count];
                Offset = 0;
                for (Index = 0; Index < Count; Index++)
                {
                    Length = Lengths[Index];
                    Offset = ALIGN_UP_BY(Offset, 8);
                    if (Length > BufferSize - DataOffset || Offset > BufferSize - DataOffset - Length)
                    {
                        Status = STATUS_INVALID_BUFFER_SIZE;
                        break;
                    }
                    AllData->OffsetInstanceDataAndLength[Index].OffsetInstanceData = DataOffset + Offset;
                    AllData->OffsetInstanceDataAndLength[Index].LengthInstanceData = Length;
                    Offset += Length;
                }
                if (!NT_SUCCESS(Status))
                    break;

                if (Offset > BufferUsed)
                    BufferUsed = Offset;

                AllData->WnodeHeader.Flags &= ~WNODE_FLAG_FIXED_INSTANCE_SIZE;
                AllData->WnodeHeader.BufferSize = DataOffset + BufferUsed;
                KeQuerySystemTime(&AllData->WnodeHeader.TimeStamp);
                Information = AllData->WnodeHeader.BufferSize;
                break;
            }
            case IRP_MN_QUERY_SINGLE_INSTANCE:
                Instance = (PWNODE_SINGLE_INSTANCE)Buffer;
                Instance->WnodeHeader.BufferSize = DataOffset + Instance->SizeDataBlock;
                KeQuerySystemTime(&Instance->WnodeHeader.TimeStamp);
                Information = Instance->WnodeHeader.BufferSize;
                break;
            case IRP_MN_EXECUTE_METHOD:
                Method = (PWNODE_METHOD_ITEM)Buffer;
                Method->SizeDataBlock = BufferUsed;
                Method->WnodeHeader.BufferSize = DataOffset + BufferUsed;
                Information = Method->WnodeHeader.BufferSize;
                break;
            default:
                break;
        }
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, PriorityBoost);
    return Status;
}

NTSTATUS
NTAPI
WmiFireEvent(IN PDEVICE_OBJECT DeviceObject,
             IN LPCGUID Guid,
             IN ULONG InstanceIndex,
             IN ULONG EventDataSize,
             IN PVOID EventData)
{
    ULONG DataOffset = ALIGN_UP_BY(FIELD_OFFSET(WNODE_SINGLE_INSTANCE, VariableData), 8);
    PWNODE_SINGLE_INSTANCE Event = NULL;
    NTSTATUS Status;

    if (EventDataSize <= MAXULONG - DataOffset)
        Event = ExAllocatePoolZero(NonPagedPoolNx, DataOffset + EventDataSize, TAG_WMILIB);

    if (!Event)
    {
        if (EventData)
            ExFreePool(EventData);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Event->WnodeHeader.BufferSize = DataOffset + EventDataSize;
    Event->WnodeHeader.ProviderId = IoWMIDeviceObjectToProviderId(DeviceObject);
    Event->WnodeHeader.Guid = *Guid;
    Event->WnodeHeader.Flags = WNODE_FLAG_EVENT_ITEM |
                               WNODE_FLAG_SINGLE_INSTANCE |
                               WNODE_FLAG_STATIC_INSTANCE_NAMES;
    KeQuerySystemTime(&Event->WnodeHeader.TimeStamp);
    Event->InstanceIndex = InstanceIndex;
    Event->DataBlockOffset = DataOffset;
    Event->SizeDataBlock = EventDataSize;
    if (EventDataSize)
        RtlCopyMemory((PUCHAR)Event + DataOffset, EventData, EventDataSize);
    if (EventData)
        ExFreePool(EventData);

    Status = IoWMIWriteEvent(Event);
    if (!NT_SUCCESS(Status))
        ExFreePoolWithTag(Event, TAG_WMILIB);
    return Status;
}

static
NTSTATUS
WmiLibNotCompleted(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _Out_ PSYSCTL_IRP_DISPOSITION IrpDisposition)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    *IrpDisposition = IrpNotCompleted;
    return Status;
}

NTSTATUS
NTAPI
WmiSystemControl(IN PWMILIB_CONTEXT WmiLibInfo,
                 IN PDEVICE_OBJECT DeviceObject,
                 IN PIRP Irp,
                 OUT PSYSCTL_IRP_DISPOSITION IrpDisposition)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    UCHAR MinorFunction = Stack->MinorFunction;
    PUCHAR Buffer = Stack->Parameters.WMI.Buffer;
    ULONG BufferSize = Stack->Parameters.WMI.BufferSize;
    ULONG GuidIndex, Count, DataOffset, BufferAvail;
    PWNODE_ALL_DATA AllData;
    PWNODE_SINGLE_INSTANCE Instance;
    PWNODE_SINGLE_ITEM Item;
    PWNODE_METHOD_ITEM Method;
    PULONG Lengths;
    NTSTATUS Status;

    if (MinorFunction > IRP_MN_EXECUTE_METHOD && MinorFunction != IRP_MN_REGINFO_EX)
    {
        *IrpDisposition = IrpNotWmi;
        return Irp->IoStatus.Status;
    }

    if ((PDEVICE_OBJECT)Stack->Parameters.WMI.ProviderId != DeviceObject)
    {
        *IrpDisposition = IrpForward;
        return Irp->IoStatus.Status;
    }

    if (MinorFunction == IRP_MN_REGINFO || MinorFunction == IRP_MN_REGINFO_EX)
    {
        Status = WmiLibRegInfo(WmiLibInfo, DeviceObject, Irp, Stack);
        Irp->IoStatus.Status = Status;
        *IrpDisposition = IrpNotCompleted;
        return Status;
    }

    if (!WmiLibFindGuid(WmiLibInfo, Stack->Parameters.WMI.DataPath, &GuidIndex))
    {
        return WmiLibNotCompleted(Irp, STATUS_WMI_GUID_NOT_FOUND, IrpDisposition);
    }

    *IrpDisposition = IrpProcessed;

    switch (MinorFunction)
    {
        case IRP_MN_QUERY_ALL_DATA:
        {
            if (!WmiLibInfo->QueryWmiDataBlock)
            {
                return WmiLibNotCompleted(Irp, STATUS_INVALID_DEVICE_REQUEST, IrpDisposition);
            }
            if (BufferSize < FIELD_OFFSET(WNODE_ALL_DATA, OffsetInstanceDataAndLength))
            {
                return WmiLibNotCompleted(Irp, STATUS_BUFFER_TOO_SMALL, IrpDisposition);
            }

            Count = WmiLibInfo->GuidList[GuidIndex].InstanceCount;
            DataOffset = ALIGN_UP_BY(FIELD_OFFSET(WNODE_ALL_DATA, OffsetInstanceDataAndLength) +
                                     Count * (sizeof(OFFSETINSTANCEDATAANDLENGTH) + sizeof(ULONG)),
                                     8);
            AllData = (PWNODE_ALL_DATA)Buffer;
            AllData->InstanceCount = Count;
            AllData->DataBlockOffset = DataOffset;
            AllData->WnodeHeader.Flags &= ~WNODE_FLAG_FIXED_INSTANCE_SIZE;

            if (BufferSize < DataOffset)
            {
                return WmiLibInfo->QueryWmiDataBlock(DeviceObject, Irp, GuidIndex, 0, Count,
                                                     NULL, 0, NULL);
            }

            Lengths = (PULONG)&AllData->OffsetInstanceDataAndLength[Count];
            RtlZeroMemory(Lengths, Count * sizeof(ULONG));
            BufferAvail = BufferSize - DataOffset;
            return WmiLibInfo->QueryWmiDataBlock(DeviceObject, Irp, GuidIndex, 0, Count,
                                                 Lengths, BufferAvail,
                                                 BufferAvail ? Buffer + DataOffset : NULL);
        }

        case IRP_MN_QUERY_SINGLE_INSTANCE:
        {
            if (!WmiLibInfo->QueryWmiDataBlock)
            {
                return WmiLibNotCompleted(Irp, STATUS_INVALID_DEVICE_REQUEST, IrpDisposition);
            }
            Instance = (PWNODE_SINGLE_INSTANCE)Buffer;
            if (BufferSize < FIELD_OFFSET(WNODE_SINGLE_INSTANCE, VariableData) ||
                Instance->DataBlockOffset > BufferSize)
            {
                return WmiLibNotCompleted(Irp, STATUS_INVALID_PARAMETER, IrpDisposition);
            }
            if (!WmiLibStaticInstance(WmiLibInfo, GuidIndex, &Instance->WnodeHeader, Instance->InstanceIndex))
            {
                return WmiLibNotCompleted(Irp, STATUS_WMI_INSTANCE_NOT_FOUND, IrpDisposition);
            }

            Instance->SizeDataBlock = 0;
            BufferAvail = BufferSize - Instance->DataBlockOffset;
            return WmiLibInfo->QueryWmiDataBlock(DeviceObject, Irp, GuidIndex, Instance->InstanceIndex, 1,
                                                 BufferAvail ? &Instance->SizeDataBlock : NULL,
                                                 BufferAvail,
                                                 BufferAvail ? Buffer + Instance->DataBlockOffset : NULL);
        }

        case IRP_MN_CHANGE_SINGLE_INSTANCE:
        {
            Instance = (PWNODE_SINGLE_INSTANCE)Buffer;
            if (BufferSize < FIELD_OFFSET(WNODE_SINGLE_INSTANCE, VariableData) ||
                Instance->DataBlockOffset > BufferSize ||
                Instance->SizeDataBlock > BufferSize - Instance->DataBlockOffset)
            {
                return WmiLibNotCompleted(Irp, STATUS_INVALID_PARAMETER, IrpDisposition);
            }
            if (!WmiLibStaticInstance(WmiLibInfo, GuidIndex, &Instance->WnodeHeader, Instance->InstanceIndex))
            {
                return WmiLibNotCompleted(Irp, STATUS_WMI_INSTANCE_NOT_FOUND, IrpDisposition);
            }
            if (!WmiLibInfo->SetWmiDataBlock)
            {
                return WmiLibNotCompleted(Irp, STATUS_WMI_READ_ONLY, IrpDisposition);
            }

            return WmiLibInfo->SetWmiDataBlock(DeviceObject, Irp, GuidIndex, Instance->InstanceIndex,
                                               Instance->SizeDataBlock,
                                               Buffer + Instance->DataBlockOffset);
        }

        case IRP_MN_CHANGE_SINGLE_ITEM:
        {
            Item = (PWNODE_SINGLE_ITEM)Buffer;
            if (BufferSize < FIELD_OFFSET(WNODE_SINGLE_ITEM, VariableData) ||
                Item->DataBlockOffset > BufferSize ||
                Item->SizeDataItem > BufferSize - Item->DataBlockOffset)
            {
                return WmiLibNotCompleted(Irp, STATUS_INVALID_PARAMETER, IrpDisposition);
            }
            if (!WmiLibStaticInstance(WmiLibInfo, GuidIndex, &Item->WnodeHeader, Item->InstanceIndex))
            {
                return WmiLibNotCompleted(Irp, STATUS_WMI_INSTANCE_NOT_FOUND, IrpDisposition);
            }
            if (!WmiLibInfo->SetWmiDataItem)
            {
                return WmiLibNotCompleted(Irp, STATUS_WMI_READ_ONLY, IrpDisposition);
            }

            return WmiLibInfo->SetWmiDataItem(DeviceObject, Irp, GuidIndex, Item->InstanceIndex,
                                              Item->ItemId, Item->SizeDataItem,
                                              Buffer + Item->DataBlockOffset);
        }

        case IRP_MN_EXECUTE_METHOD:
        {
            Method = (PWNODE_METHOD_ITEM)Buffer;
            if (BufferSize < FIELD_OFFSET(WNODE_METHOD_ITEM, VariableData) ||
                Method->DataBlockOffset > BufferSize ||
                Method->SizeDataBlock > BufferSize - Method->DataBlockOffset)
            {
                return WmiLibNotCompleted(Irp, STATUS_INVALID_PARAMETER, IrpDisposition);
            }
            if (!WmiLibStaticInstance(WmiLibInfo, GuidIndex, &Method->WnodeHeader, Method->InstanceIndex))
            {
                return WmiLibNotCompleted(Irp, STATUS_WMI_INSTANCE_NOT_FOUND, IrpDisposition);
            }
            if (!WmiLibInfo->ExecuteWmiMethod)
            {
                return WmiLibNotCompleted(Irp, STATUS_INVALID_DEVICE_REQUEST, IrpDisposition);
            }

            return WmiLibInfo->ExecuteWmiMethod(DeviceObject, Irp, GuidIndex, Method->InstanceIndex,
                                                Method->MethodId, Method->SizeDataBlock,
                                                BufferSize - Method->DataBlockOffset,
                                                Buffer + Method->DataBlockOffset);
        }

        case IRP_MN_ENABLE_EVENTS:
        case IRP_MN_DISABLE_EVENTS:
        case IRP_MN_ENABLE_COLLECTION:
        case IRP_MN_DISABLE_COLLECTION:
        {
            if (!WmiLibInfo->WmiFunctionControl)
                return WmiCompleteRequest(DeviceObject, Irp, STATUS_SUCCESS, 0, IO_NO_INCREMENT);

            return WmiLibInfo->WmiFunctionControl(DeviceObject,
                                                  Irp,
                                                  GuidIndex,
                                                  (MinorFunction == IRP_MN_ENABLE_EVENTS ||
                                                   MinorFunction == IRP_MN_DISABLE_EVENTS) ?
                                                  WmiEventControl : WmiDataBlockControl,
                                                  (MinorFunction == IRP_MN_ENABLE_EVENTS ||
                                                   MinorFunction == IRP_MN_ENABLE_COLLECTION));
        }

        default:
            break;
    }

    return WmiLibNotCompleted(Irp, STATUS_INVALID_DEVICE_REQUEST, IrpDisposition);
}
