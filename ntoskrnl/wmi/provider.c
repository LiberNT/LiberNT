/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WMI data provider registration and request dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <wmistr.h>
#include "wmip.h"

#define NDEBUG
#include <debug.h>

#define TAG_WMI_PROVIDER 'pimW'
#define TAG_WMI_SET 'simW'
#define TAG_WMI_BUFFER 'bimW'
#define TAG_WMI_STATE 'gimW'

#define WMIP_PENDING_REGISTER 0x1
#define WMIP_PENDING_UPDATE 0x2

#define WMIP_INITIAL_REQUEST_SIZE 0x1000
#define WMIP_MAX_REQUEST_SIZE (16 * 1024 * 1024)
#define WMIP_MAX_RETRIES 8

#define WMIP_STATIC_NAME_FLAGS \
    (WMIREG_FLAG_INSTANCE_LIST | WMIREG_FLAG_INSTANCE_BASENAME | WMIREG_FLAG_INSTANCE_PDO)

typedef struct _WMIP_PROVIDER
{
    LIST_ENTRY Link;
    LIST_ENTRY QueueLink;
    LIST_ENTRY InstanceSets;
    PDEVICE_OBJECT DeviceObject;
    EX_RUNDOWN_REF Rundown;
    LONG ReferenceCount;
    ULONG ProviderId;
    ULONG PendingActions;
    BOOLEAN Queued;
    BOOLEAN Deregistered;
    BOOLEAN IrpsBlocked;
} WMIP_PROVIDER, *PWMIP_PROVIDER;

typedef struct _WMIP_INSTANCE_SET
{
    LIST_ENTRY Link;
    PWMIP_PROVIDER Provider;
    LONG ReferenceCount;
    GUID Guid;
    ULONG Flags;
    ULONG InstanceCount;
    ULONG BaseIndex;
    UNICODE_STRING BaseName;
    PUNICODE_STRING Names;
    PDEVICE_OBJECT Pdo;
    BOOLEAN EventsEnabled;
    BOOLEAN CollectionEnabled;
} WMIP_INSTANCE_SET, *PWMIP_INSTANCE_SET;

typedef struct _WMIP_GUID_STATE
{
    LIST_ENTRY Link;
    GUID Guid;
    ULONG OpenCount;
    ULONG EventCount;
    ULONG NextInstanceId;
} WMIP_GUID_STATE, *PWMIP_GUID_STATE;

typedef struct _WMIP_SET_LIST
{
    ULONG Count;
    PWMIP_INSTANCE_SET Sets[ANYSIZE_ARRAY];
} WMIP_SET_LIST, *PWMIP_SET_LIST;

static ERESOURCE WmipRegistrationResource;
static ERESOURCE WmipControlResource;
static LIST_ENTRY WmipProviderList;
static LIST_ENTRY WmipGuidStateList;
static LIST_ENTRY WmipRegistrationQueue;
static KSPIN_LOCK WmipQueueLock;
static KSPIN_LOCK WmipProviderLock;
static BOOLEAN WmipWorkerQueued;
static WORK_QUEUE_ITEM WmipRegistrationWorkItem;
static LONG WmipNextProviderId;

static WORKER_THREAD_ROUTINE WmipRegistrationWorker;

static
VOID
WmipAcquireShared(
    VOID)
{
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&WmipRegistrationResource, TRUE);
}

static
VOID
WmipAcquireExclusive(
    VOID)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&WmipRegistrationResource, TRUE);
}

static
VOID
WmipRelease(
    VOID)
{
    ExReleaseResourceLite(&WmipRegistrationResource);
    KeLeaveCriticalRegion();
}

static
VOID
WmipReferenceProvider(
    _In_ PWMIP_PROVIDER Provider)
{
    InterlockedIncrement(&Provider->ReferenceCount);
}

static
VOID
WmipDereferenceProvider(
    _In_ PWMIP_PROVIDER Provider)
{
    if (InterlockedDecrement(&Provider->ReferenceCount) == 0)
    {
        ObDereferenceObject(Provider->DeviceObject);
        ExFreePoolWithTag(Provider, TAG_WMI_PROVIDER);
    }
}

static
VOID
WmipDereferenceSet(
    _In_ PWMIP_INSTANCE_SET Set)
{
    if (InterlockedDecrement(&Set->ReferenceCount) == 0)
    {
        if (Set->Pdo)
            ObDereferenceObject(Set->Pdo);
        if (Set->BaseName.Buffer)
            ExFreePoolWithTag(Set->BaseName.Buffer, TAG_WMI_SET);
        if (Set->Names)
            ExFreePoolWithTag(Set->Names, TAG_WMI_SET);
        WmipDereferenceProvider(Set->Provider);
        ExFreePoolWithTag(Set, TAG_WMI_SET);
    }
}

static
PWMIP_PROVIDER
WmipFindProvider(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PLIST_ENTRY Entry;
    PWMIP_PROVIDER Provider;

    for (Entry = WmipProviderList.Flink; Entry != &WmipProviderList; Entry = Entry->Flink)
    {
        Provider = CONTAINING_RECORD(Entry, WMIP_PROVIDER, Link);
        if (Provider->DeviceObject == DeviceObject)
            return Provider;
    }

    return NULL;
}

static
PWMIP_GUID_STATE
WmipFindGuidState(
    _In_ LPCGUID Guid,
    _In_ BOOLEAN Create)
{
    PLIST_ENTRY Entry;
    PWMIP_GUID_STATE State;

    for (Entry = WmipGuidStateList.Flink; Entry != &WmipGuidStateList; Entry = Entry->Flink)
    {
        State = CONTAINING_RECORD(Entry, WMIP_GUID_STATE, Link);
        if (IsEqualGUID(&State->Guid, Guid))
            return State;
    }

    if (!Create)
        return NULL;

    State = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*State), TAG_WMI_STATE);
    if (!State)
        return NULL;

    State->Guid = *Guid;
    InsertTailList(&WmipGuidStateList, &State->Link);
    return State;
}

static
VOID
WmipReleaseGuidState(
    _In_ PWMIP_GUID_STATE State)
{
    if (!State->OpenCount && !State->EventCount && !State->NextInstanceId)
    {
        RemoveEntryList(&State->Link);
        ExFreePoolWithTag(State, TAG_WMI_STATE);
    }
}

static
NTSTATUS
NTAPI
WmipIrpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
WmipSendIrp(
    _In_ PWMIP_PROVIDER Provider,
    _In_ UCHAR MinorFunction,
    _In_ PVOID DataPath,
    _In_ ULONG BufferSize,
    _Inout_ PVOID Buffer,
    _Out_ PULONG_PTR Information)
{
    PDEVICE_OBJECT Target;
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;

    PAGED_CODE();

    *Information = 0;
    if (Provider->IrpsBlocked)
        return STATUS_WMI_GUID_DISCONNECTED;

    Target = IoGetAttachedDeviceReference(Provider->DeviceObject);
    Irp = IoAllocateIrp(Target->StackSize, FALSE);
    if (!Irp)
    {
        ObDereferenceObject(Target);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Irp->RequestorMode = KernelMode;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_SYSTEM_CONTROL;
    Stack->MinorFunction = MinorFunction;
    Stack->Parameters.WMI.ProviderId = (ULONG_PTR)Provider->DeviceObject;
    Stack->Parameters.WMI.DataPath = DataPath;
    Stack->Parameters.WMI.BufferSize = BufferSize;
    Stack->Parameters.WMI.Buffer = Buffer;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, WmipIrpCompletion, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(Target, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

    Status = Irp->IoStatus.Status;
    *Information = Irp->IoStatus.Information;

    IoFreeIrp(Irp);
    ObDereferenceObject(Target);
    return Status;
}

static
BOOLEAN
WmipCaptureCountedString(
    _In_ PUCHAR Base,
    _In_ ULONG Limit,
    _In_ ULONG Offset,
    _Out_ PUNICODE_STRING String)
{
    USHORT Length;

    if ((Offset & 1) || Offset > Limit || Limit - Offset < sizeof(USHORT))
        return FALSE;

    Length = *(PUSHORT)(Base + Offset);
    if ((Length & 1) || Limit - Offset - sizeof(USHORT) < Length)
        return FALSE;

    String->Buffer = (PWCHAR)(Base + Offset + sizeof(USHORT));
    String->Length = Length;
    String->MaximumLength = Length;

    while (String->Length >= sizeof(WCHAR) &&
           String->Buffer[String->Length / sizeof(WCHAR) - 1] == UNICODE_NULL)
    {
        String->Length -= sizeof(WCHAR);
    }

    return TRUE;
}

static
NTSTATUS
WmipDuplicateString(
    _In_ PCUNICODE_STRING Source,
    _In_ PCWSTR Suffix,
    _Out_ PUNICODE_STRING Destination)
{
    SIZE_T SuffixLength = Suffix ? wcslen(Suffix) * sizeof(WCHAR) : 0;
    SIZE_T Length = Source->Length + SuffixLength;

    if (Length > MAXUSHORT - sizeof(WCHAR))
        return STATUS_INVALID_PARAMETER;

    Destination->Buffer = ExAllocatePoolWithTag(NonPagedPoolNx, Length + sizeof(WCHAR), TAG_WMI_SET);
    if (!Destination->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Destination->Buffer, Source->Buffer, Source->Length);
    if (SuffixLength)
        RtlCopyMemory((PUCHAR)Destination->Buffer + Source->Length, Suffix, SuffixLength);
    Destination->Length = (USHORT)Length;
    Destination->MaximumLength = (USHORT)(Length + sizeof(WCHAR));
    Destination->Buffer[Length / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
ULONG
WmipFormatIndex(
    _In_ ULONG Value,
    _Out_writes_(10) PWCHAR Digits)
{
    WCHAR Reverse[10];
    ULONG Count = 0, Index;

    do
    {
        Reverse[Count++] = (WCHAR)(L'0' + Value % 10);
        Value /= 10;
    } while (Value);

    for (Index = 0; Index < Count; Index++)
        Digits[Index] = Reverse[Count - Index - 1];

    return Count;
}

static
USHORT
WmipInstanceNameLength(
    _In_ PWMIP_INSTANCE_SET Set,
    _In_ ULONG Index)
{
    WCHAR Digits[10];
    ULONG DigitCount;

    if (Set->Flags & WMIREG_FLAG_INSTANCE_LIST)
        return Set->Names[Index].Length;

    DigitCount = WmipFormatIndex(Set->BaseIndex + Index, Digits);
    return (USHORT)(Set->BaseName.Length + DigitCount * sizeof(WCHAR));
}

static
VOID
WmipWriteInstanceName(
    _In_ PWMIP_INSTANCE_SET Set,
    _In_ ULONG Index,
    _Out_writes_bytes_(sizeof(USHORT) + WmipInstanceNameLength(Set, Index)) PUCHAR Destination)
{
    WCHAR Digits[10];
    ULONG DigitCount;
    USHORT Length = WmipInstanceNameLength(Set, Index);

    RtlCopyMemory(Destination, &Length, sizeof(USHORT));
    Destination += sizeof(USHORT);

    if (Set->Flags & WMIREG_FLAG_INSTANCE_LIST)
    {
        RtlCopyMemory(Destination, Set->Names[Index].Buffer, Length);
        return;
    }

    DigitCount = WmipFormatIndex(Set->BaseIndex + Index, Digits);
    RtlCopyMemory(Destination, Set->BaseName.Buffer, Set->BaseName.Length);
    RtlCopyMemory(Destination + Set->BaseName.Length, Digits, DigitCount * sizeof(WCHAR));
}

static
NTSTATUS
WmipAllocateInstanceName(
    _In_ PWMIP_INSTANCE_SET Set,
    _In_ ULONG Index,
    _Out_ PUNICODE_STRING Name)
{
    USHORT Length = WmipInstanceNameLength(Set, Index);
    PUCHAR Buffer;

    Buffer = ExAllocatePoolWithTag(PagedPool, sizeof(USHORT) + Length + sizeof(WCHAR), TAG_WMI_BUFFER);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    WmipWriteInstanceName(Set, Index, Buffer);
    RtlMoveMemory(Buffer, Buffer + sizeof(USHORT), Length);
    Name->Buffer = (PWCHAR)Buffer;
    Name->Length = Length;
    Name->MaximumLength = Length + sizeof(WCHAR);
    Name->Buffer[Length / sizeof(WCHAR)] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
BOOLEAN
WmipMatchStaticName(
    _In_ PWMIP_INSTANCE_SET Set,
    _In_ PCUNICODE_STRING Name,
    _Out_ PULONG Index)
{
    UNICODE_STRING Prefix, Digits;
    ULONG Value = 0, Position;

    if (Set->Flags & WMIREG_FLAG_INSTANCE_LIST)
    {
        for (Position = 0; Position < Set->InstanceCount; Position++)
        {
            if (RtlEqualUnicodeString(&Set->Names[Position], Name, TRUE))
            {
                *Index = Position;
                return TRUE;
            }
        }
        return FALSE;
    }

    if (Name->Length <= Set->BaseName.Length)
        return FALSE;

    Prefix.Buffer = Name->Buffer;
    Prefix.Length = Prefix.MaximumLength = Set->BaseName.Length;
    if (!RtlEqualUnicodeString(&Prefix, &Set->BaseName, TRUE))
        return FALSE;

    Digits.Buffer = Name->Buffer + Set->BaseName.Length / sizeof(WCHAR);
    Digits.Length = Digits.MaximumLength = Name->Length - Set->BaseName.Length;
    for (Position = 0; Position < Digits.Length / sizeof(WCHAR); Position++)
    {
        if (Digits.Buffer[Position] < L'0' || Digits.Buffer[Position] > L'9')
            return FALSE;
        if (Value > (MAXULONG - 9) / 10)
            return FALSE;
        Value = Value * 10 + (Digits.Buffer[Position] - L'0');
    }

    if (Value < Set->BaseIndex || Value - Set->BaseIndex >= Set->InstanceCount)
        return FALSE;

    *Index = Value - Set->BaseIndex;
    return TRUE;
}

static
NTSTATUS
WmipCaptureInstanceList(
    _Inout_ PWMIP_INSTANCE_SET Set,
    _In_ PUCHAR RegInfoBase,
    _In_ ULONG RegInfoLimit,
    _In_ PWMIREGGUIDW RegGuid)
{
    UNICODE_STRING Name;
    ULONG Offset, Index;
    SIZE_T NamesSize;
    PUCHAR Strings;

    if (RegGuid->InstanceNameList > MAXULONG || Set->InstanceCount > RegInfoLimit / sizeof(USHORT))
        return STATUS_INVALID_PARAMETER;

    NamesSize = Set->InstanceCount * sizeof(UNICODE_STRING);
    Offset = (ULONG)RegGuid->InstanceNameList;
    for (Index = 0; Index < Set->InstanceCount; Index++)
    {
        if (!WmipCaptureCountedString(RegInfoBase, RegInfoLimit, Offset, &Name))
            return STATUS_INVALID_PARAMETER;
        NamesSize += ALIGN_UP_BY(Name.Length + sizeof(WCHAR), sizeof(WCHAR));
        Offset += sizeof(USHORT) + Name.MaximumLength;
        Offset = ALIGN_UP_BY(Offset, sizeof(USHORT));
    }

    if (!Set->InstanceCount)
        return STATUS_SUCCESS;

    Set->Names = ExAllocatePoolZero(NonPagedPoolNx, NamesSize, TAG_WMI_SET);
    if (!Set->Names)
        return STATUS_INSUFFICIENT_RESOURCES;

    Strings = (PUCHAR)&Set->Names[Set->InstanceCount];
    Offset = (ULONG)RegGuid->InstanceNameList;
    for (Index = 0; Index < Set->InstanceCount; Index++)
    {
        WmipCaptureCountedString(RegInfoBase, RegInfoLimit, Offset, &Name);
        Offset += sizeof(USHORT) + Name.MaximumLength;
        Offset = ALIGN_UP_BY(Offset, sizeof(USHORT));

        RtlCopyMemory(Strings, Name.Buffer, Name.Length);
        Set->Names[Index].Buffer = (PWCHAR)Strings;
        Set->Names[Index].Length = Name.Length;
        Set->Names[Index].MaximumLength = Name.Length + sizeof(WCHAR);
        Strings += ALIGN_UP_BY(Name.Length + sizeof(WCHAR), sizeof(WCHAR));
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipCaptureInstanceNames(
    _Inout_ PWMIP_INSTANCE_SET Set,
    _In_ PUCHAR RegInfoBase,
    _In_ ULONG RegInfoLimit,
    _In_ PWMIREGGUIDW RegGuid,
    _In_ BOOLEAN OwnsPdoReference)
{
    UNICODE_STRING Name;
    PDEVICE_NODE DeviceNode;

    Set->InstanceCount = RegGuid->InstanceCount;

    if (RegGuid->Flags & WMIREG_FLAG_INSTANCE_PDO)
    {
        Set->Flags &= ~(WMIREG_FLAG_INSTANCE_LIST | WMIREG_FLAG_INSTANCE_BASENAME);
        Set->Pdo = (PDEVICE_OBJECT)RegGuid->Pdo;
        if (Set->Pdo && !OwnsPdoReference)
            ObReferenceObject(Set->Pdo);

        DeviceNode = Set->Pdo ? IopGetDeviceNode(Set->Pdo) : NULL;
        if (!DeviceNode || !DeviceNode->InstancePath.Length)
            return STATUS_INVALID_PARAMETER;

        return WmipDuplicateString(&DeviceNode->InstancePath, L"_", &Set->BaseName);
    }

    if (RegGuid->Flags & WMIREG_FLAG_INSTANCE_BASENAME)
    {
        Set->Flags &= ~WMIREG_FLAG_INSTANCE_LIST;
        if (RegGuid->BaseNameOffset > MAXULONG ||
            !WmipCaptureCountedString(RegInfoBase, RegInfoLimit, (ULONG)RegGuid->BaseNameOffset, &Name))
        {
            return STATUS_INVALID_PARAMETER;
        }

        return WmipDuplicateString(&Name, NULL, &Set->BaseName);
    }

    if (RegGuid->Flags & WMIREG_FLAG_INSTANCE_LIST)
        return WmipCaptureInstanceList(Set, RegInfoBase, RegInfoLimit, RegGuid);

    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipCreateSet(
    _In_ PWMIP_PROVIDER Provider,
    _In_ PUCHAR RegInfoBase,
    _In_ ULONG RegInfoLimit,
    _In_ PWMIREGGUIDW RegGuid,
    _In_ BOOLEAN OwnsPdoReference,
    _Out_ PWMIP_INSTANCE_SET *OutSet)
{
    PWMIP_INSTANCE_SET Set;
    NTSTATUS Status;

    *OutSet = NULL;

    Set = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Set), TAG_WMI_SET);
    if (!Set)
        return STATUS_INSUFFICIENT_RESOURCES;

    Set->Provider = Provider;
    Set->ReferenceCount = 1;
    Set->Guid = RegGuid->Guid;
    Set->Flags = RegGuid->Flags;

    Status = WmipCaptureInstanceNames(Set, RegInfoBase, RegInfoLimit, RegGuid, OwnsPdoReference);
    if (!NT_SUCCESS(Status))
    {
        if (Set->Pdo)
            ObDereferenceObject(Set->Pdo);
        if (Set->BaseName.Buffer)
            ExFreePoolWithTag(Set->BaseName.Buffer, TAG_WMI_SET);
        if (Set->Names)
            ExFreePoolWithTag(Set->Names, TAG_WMI_SET);
        ExFreePoolWithTag(Set, TAG_WMI_SET);
        return Status;
    }

    WmipReferenceProvider(Provider);
    *OutSet = Set;
    return STATUS_SUCCESS;
}

static
ULONG
WmipComputeBaseIndex(
    _In_ PWMIP_INSTANCE_SET NewSet)
{
    PLIST_ENTRY ProviderEntry, SetEntry;
    PWMIP_PROVIDER Provider;
    PWMIP_INSTANCE_SET Set;
    ULONG BaseIndex = 0;

    if (!(NewSet->Flags & (WMIREG_FLAG_INSTANCE_BASENAME | WMIREG_FLAG_INSTANCE_PDO)))
        return 0;

    for (ProviderEntry = WmipProviderList.Flink; ProviderEntry != &WmipProviderList; ProviderEntry = ProviderEntry->Flink)
    {
        Provider = CONTAINING_RECORD(ProviderEntry, WMIP_PROVIDER, Link);
        for (SetEntry = Provider->InstanceSets.Flink; SetEntry != &Provider->InstanceSets; SetEntry = SetEntry->Flink)
        {
            Set = CONTAINING_RECORD(SetEntry, WMIP_INSTANCE_SET, Link);
            if (Set != NewSet &&
                IsEqualGUID(&Set->Guid, &NewSet->Guid) &&
                (Set->Flags & (WMIREG_FLAG_INSTANCE_BASENAME | WMIREG_FLAG_INSTANCE_PDO)) &&
                RtlEqualUnicodeString(&Set->BaseName, &NewSet->BaseName, TRUE) &&
                Set->BaseIndex + Set->InstanceCount > BaseIndex)
            {
                BaseIndex = Set->BaseIndex + Set->InstanceCount;
            }
        }
    }

    return BaseIndex;
}

static
VOID
WmipReleaseUnusedPdoReferences(
    _In_ PUCHAR Buffer,
    _In_ ULONG Size,
    _In_ ULONG FirstUnused)
{
    PWMIREGINFOW RegInfo;
    ULONG Offset = 0, Index, Ordinal = 0;

    for (;;)
    {
        if (Offset > Size || Size - Offset < sizeof(WMIREGINFOW))
            return;

        RegInfo = (PWMIREGINFOW)(Buffer + Offset);
        if (RegInfo->GuidCount > (Size - Offset - sizeof(WMIREGINFOW)) / sizeof(WMIREGGUIDW))
            return;

        for (Index = 0; Index < RegInfo->GuidCount; Index++, Ordinal++)
        {
            if (Ordinal >= FirstUnused &&
                (RegInfo->WmiRegGuid[Index].Flags & WMIREG_FLAG_INSTANCE_PDO) &&
                RegInfo->WmiRegGuid[Index].Pdo)
            {
                ObDereferenceObject((PDEVICE_OBJECT)RegInfo->WmiRegGuid[Index].Pdo);
            }
        }

        if (!RegInfo->NextWmiRegInfo || RegInfo->NextWmiRegInfo > Size - Offset)
            return;
        Offset += RegInfo->NextWmiRegInfo;
    }
}

static
NTSTATUS
WmipQueryRegInfo(
    _In_ PWMIP_PROVIDER Provider,
    _In_ ULONG DataPath,
    _Out_ PUCHAR *OutBuffer,
    _Out_ PULONG OutSize,
    _Out_ PBOOLEAN OwnsPdoReferences)
{
    static const UCHAR Minors[] = { IRP_MN_REGINFO_EX, IRP_MN_REGINFO };
    ULONG MinorIndex, Attempt, Size, Needed;
    ULONG_PTR Information;
    PUCHAR Buffer;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    for (MinorIndex = 0; MinorIndex < RTL_NUMBER_OF(Minors); MinorIndex++)
    {
        Size = WMIP_INITIAL_REQUEST_SIZE;
        for (Attempt = 0; Attempt < WMIP_MAX_RETRIES; Attempt++)
        {
            Buffer = ExAllocatePoolZero(NonPagedPoolNx, Size, TAG_WMI_BUFFER);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            Status = WmipSendIrp(Provider,
                                 Minors[MinorIndex],
                                 (PVOID)(ULONG_PTR)DataPath,
                                 Size,
                                 Buffer,
                                 &Information);
            if (Status == STATUS_BUFFER_TOO_SMALL)
            {
                Needed = *(PULONG)Buffer;
                ExFreePoolWithTag(Buffer, TAG_WMI_BUFFER);
                if (Needed <= Size || Needed > WMIP_MAX_REQUEST_SIZE)
                    break;
                Size = ALIGN_UP_BY(Needed, sizeof(ULONGLONG));
                continue;
            }

            if (NT_SUCCESS(Status))
            {
                if (Information < sizeof(WMIREGINFOW) || Information > Size ||
                    ((PWMIREGINFOW)Buffer)->BufferSize > Size)
                {
                    if (Minors[MinorIndex] == IRP_MN_REGINFO_EX)
                        WmipReleaseUnusedPdoReferences(Buffer, Size, 0);
                    ExFreePoolWithTag(Buffer, TAG_WMI_BUFFER);
                    return STATUS_INVALID_PARAMETER;
                }

                *OutBuffer = Buffer;
                *OutSize = (ULONG)Information;
                *OwnsPdoReferences = (Minors[MinorIndex] == IRP_MN_REGINFO_EX);
                return STATUS_SUCCESS;
            }

            ExFreePoolWithTag(Buffer, TAG_WMI_BUFFER);
            break;
        }

        if (Provider->Deregistered)
            break;
    }

    return Status;
}

static
VOID
WmipUpdateGuidControl(
    _In_ LPCGUID Guid);

static
VOID
WmipApplyRegInfo(
    _In_ PWMIP_PROVIDER Provider,
    _In_ ULONG DataPath,
    _In_ PUCHAR Buffer,
    _In_ ULONG Size,
    _In_ BOOLEAN OwnsPdoReferences)
{
    LIST_ENTRY NewSets, RemovedSets;
    PWMIREGINFOW RegInfo;
    PWMIREGGUIDW RegGuid;
    PWMIP_INSTANCE_SET Set, Existing;
    PLIST_ENTRY Entry, Next;
    ULONG Offset = 0, Limit, Index, Ordinal = 0, GuidCount = 0, Position;
    PGUID Guids = NULL;
    NTSTATUS Status;

    InitializeListHead(&NewSets);
    InitializeListHead(&RemovedSets);

    for (;;)
    {
        if (Offset > Size || Size - Offset < sizeof(WMIREGINFOW))
            break;

        RegInfo = (PWMIREGINFOW)(Buffer + Offset);
        Limit = Size - Offset;
        if (RegInfo->BufferSize && RegInfo->BufferSize < Limit)
            Limit = RegInfo->BufferSize;
        if (Limit < sizeof(WMIREGINFOW) ||
            RegInfo->GuidCount > (Limit - sizeof(WMIREGINFOW)) / sizeof(WMIREGGUIDW))
        {
            break;
        }

        GuidCount += RegInfo->GuidCount;
        for (Index = 0; Index < RegInfo->GuidCount; Index++, Ordinal++)
        {
            RegGuid = &RegInfo->WmiRegGuid[Index];
            if ((DataPath == WMIUPDATE) && (RegGuid->Flags & WMIREG_FLAG_REMOVE_GUID))
            {
                Set = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Set), TAG_WMI_SET);
                if (!Set)
                    continue;
                Set->Provider = Provider;
                Set->ReferenceCount = 1;
                Set->Guid = RegGuid->Guid;
                Set->Flags = WMIREG_FLAG_REMOVE_GUID;
                WmipReferenceProvider(Provider);
                InsertTailList(&NewSets, &Set->Link);
                continue;
            }

            if (RegGuid->Flags & WMIREG_FLAG_TRACED_GUID)
            {
                if (OwnsPdoReferences && (RegGuid->Flags & WMIREG_FLAG_INSTANCE_PDO) && RegGuid->Pdo)
                    ObDereferenceObject((PDEVICE_OBJECT)RegGuid->Pdo);
                continue;
            }

            Status = WmipCreateSet(Provider, (PUCHAR)RegInfo, Limit, RegGuid, OwnsPdoReferences, &Set);
            if (NT_SUCCESS(Status))
            {
                InsertTailList(&NewSets, &Set->Link);
            }
            else
            {
                DPRINT1("WMI: provider %p block %d rejected: 0x%lx\n",
                        Provider->DeviceObject, Ordinal, Status);
            }
        }

        if (!RegInfo->NextWmiRegInfo || RegInfo->NextWmiRegInfo > Size - Offset)
            break;
        Offset += RegInfo->NextWmiRegInfo;
    }

    if (GuidCount)
        Guids = ExAllocatePoolWithTag(PagedPool, GuidCount * sizeof(GUID), TAG_WMI_BUFFER);
    GuidCount = 0;

    WmipAcquireExclusive();
    if (Provider->Deregistered)
    {
        while (!IsListEmpty(&NewSets))
            InsertTailList(&RemovedSets, RemoveHeadList(&NewSets));
    }
    else
    {
        if (DataPath == WMIREGISTER)
        {
            while (!IsListEmpty(&Provider->InstanceSets))
                InsertTailList(&RemovedSets, RemoveHeadList(&Provider->InstanceSets));
        }

        while (!IsListEmpty(&NewSets))
        {
            Set = CONTAINING_RECORD(RemoveHeadList(&NewSets), WMIP_INSTANCE_SET, Link);
            if (DataPath == WMIUPDATE)
            {
                for (Entry = Provider->InstanceSets.Flink; Entry != &Provider->InstanceSets; Entry = Next)
                {
                    Next = Entry->Flink;
                    Existing = CONTAINING_RECORD(Entry, WMIP_INSTANCE_SET, Link);
                    if (IsEqualGUID(&Existing->Guid, &Set->Guid))
                    {
                        if (!(Set->Flags & WMIREG_FLAG_REMOVE_GUID))
                        {
                            Set->EventsEnabled = Existing->EventsEnabled;
                            Set->CollectionEnabled = Existing->CollectionEnabled;
                        }
                        RemoveEntryList(Entry);
                        InsertTailList(&RemovedSets, Entry);
                    }
                }
            }

            if (Set->Flags & WMIREG_FLAG_REMOVE_GUID)
            {
                InsertTailList(&RemovedSets, &Set->Link);
                continue;
            }

            Set->BaseIndex = WmipComputeBaseIndex(Set);
            InsertTailList(&Provider->InstanceSets, &Set->Link);

            if (Guids)
            {
                for (Position = 0; Position < GuidCount; Position++)
                {
                    if (IsEqualGUID(&Guids[Position], &Set->Guid))
                        break;
                }
                if (Position == GuidCount)
                    Guids[GuidCount++] = Set->Guid;
            }
        }
    }
    WmipRelease();

    while (!IsListEmpty(&RemovedSets))
    {
        Set = CONTAINING_RECORD(RemoveHeadList(&RemovedSets), WMIP_INSTANCE_SET, Link);
        WmipDereferenceSet(Set);
    }

    if (Guids)
    {
        for (Position = 0; Position < GuidCount; Position++)
            WmipUpdateGuidControl(&Guids[Position]);
        ExFreePoolWithTag(Guids, TAG_WMI_BUFFER);
    }
}

static
VOID
WmipProcessProvider(
    _In_ PWMIP_PROVIDER Provider,
    _In_ ULONG DataPath)
{
    BOOLEAN OwnsPdoReferences;
    PUCHAR Buffer;
    ULONG Size;
    NTSTATUS Status;

    if (!ExAcquireRundownProtection(&Provider->Rundown))
        return;

    Status = WmipQueryRegInfo(Provider, DataPath, &Buffer, &Size, &OwnsPdoReferences);
    if (NT_SUCCESS(Status))
    {
        WmipApplyRegInfo(Provider, DataPath, Buffer, Size, OwnsPdoReferences);
        ExFreePoolWithTag(Buffer, TAG_WMI_BUFFER);
    }
    else
    {
        DPRINT("WMI: registration query for %p failed: 0x%lx\n", Provider->DeviceObject, Status);
    }

    ExReleaseRundownProtection(&Provider->Rundown);
}

static
VOID
NTAPI
WmipRegistrationWorker(
    _In_ PVOID Context)
{
    PWMIP_PROVIDER Provider;
    ULONG Actions;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Context);

    for (;;)
    {
        KeAcquireSpinLock(&WmipQueueLock, &OldIrql);
        if (IsListEmpty(&WmipRegistrationQueue))
        {
            WmipWorkerQueued = FALSE;
            KeReleaseSpinLock(&WmipQueueLock, OldIrql);
            break;
        }

        Provider = CONTAINING_RECORD(RemoveHeadList(&WmipRegistrationQueue), WMIP_PROVIDER, QueueLink);
        Actions = Provider->PendingActions;
        Provider->PendingActions = 0;
        Provider->Queued = FALSE;
        KeReleaseSpinLock(&WmipQueueLock, OldIrql);

        if (!Provider->Deregistered)
        {
            if (Actions & WMIP_PENDING_REGISTER)
                WmipProcessProvider(Provider, WMIREGISTER);
            else if (Actions & WMIP_PENDING_UPDATE)
                WmipProcessProvider(Provider, WMIUPDATE);
        }

        WmipDereferenceProvider(Provider);
    }
}

static
VOID
WmipQueueAction(
    _In_ PWMIP_PROVIDER Provider,
    _In_ ULONG Action)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&WmipQueueLock, &OldIrql);
    Provider->PendingActions |= Action;
    if (!Provider->Queued)
    {
        Provider->Queued = TRUE;
        WmipReferenceProvider(Provider);
        InsertTailList(&WmipRegistrationQueue, &Provider->QueueLink);
    }
    if (!WmipWorkerQueued)
    {
        WmipWorkerQueued = TRUE;
        ExQueueWorkItem(&WmipRegistrationWorkItem, DelayedWorkQueue);
    }
    KeReleaseSpinLock(&WmipQueueLock, OldIrql);
}

static
NTSTATUS
WmipRegisterProvider(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PWMIP_PROVIDER Provider, Existing;
    KIRQL OldIrql;

    Provider = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*Provider), TAG_WMI_PROVIDER);
    if (!Provider)
        return STATUS_INSUFFICIENT_RESOURCES;

    ObReferenceObject(DeviceObject);
    Provider->DeviceObject = DeviceObject;
    Provider->ReferenceCount = 1;
    Provider->ProviderId = (ULONG)InterlockedIncrement(&WmipNextProviderId);
    ExInitializeRundownProtection(&Provider->Rundown);
    InitializeListHead(&Provider->InstanceSets);

    WmipAcquireExclusive();
    Existing = WmipFindProvider(DeviceObject);
    if (Existing)
    {
        WmipReferenceProvider(Existing);
    }
    else
    {
        WmipReferenceProvider(Provider);
        KeAcquireSpinLock(&WmipProviderLock, &OldIrql);
        InsertTailList(&WmipProviderList, &Provider->Link);
        KeReleaseSpinLock(&WmipProviderLock, OldIrql);
    }
    WmipRelease();

    if (Existing)
    {
        WmipDereferenceProvider(Provider);
        Provider = Existing;
    }

    WmipQueueAction(Provider, WMIP_PENDING_REGISTER);
    WmipDereferenceProvider(Provider);
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipDeregisterProvider(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    LIST_ENTRY RemovedSets;
    PWMIP_PROVIDER Provider;
    PWMIP_INSTANCE_SET Set;
    KIRQL OldIrql;

    InitializeListHead(&RemovedSets);

    WmipAcquireExclusive();
    Provider = WmipFindProvider(DeviceObject);
    if (Provider)
    {
        KeAcquireSpinLock(&WmipProviderLock, &OldIrql);
        RemoveEntryList(&Provider->Link);
        KeReleaseSpinLock(&WmipProviderLock, OldIrql);
        Provider->Deregistered = TRUE;
        while (!IsListEmpty(&Provider->InstanceSets))
            InsertTailList(&RemovedSets, RemoveHeadList(&Provider->InstanceSets));
    }
    WmipRelease();

    if (!Provider)
        return STATUS_INVALID_PARAMETER;

    ExWaitForRundownProtectionRelease(&Provider->Rundown);

    while (!IsListEmpty(&RemovedSets))
    {
        Set = CONTAINING_RECORD(RemoveHeadList(&RemovedSets), WMIP_INSTANCE_SET, Link);
        WmipDereferenceSet(Set);
    }

    WmipDereferenceProvider(Provider);
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipQueueProviderAction(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG Action)
{
    PWMIP_PROVIDER Provider;

    WmipAcquireShared();
    Provider = WmipFindProvider(DeviceObject);
    if (Provider)
        WmipReferenceProvider(Provider);
    WmipRelease();

    if (!Provider)
        return STATUS_INVALID_PARAMETER;

    WmipQueueAction(Provider, Action);
    WmipDereferenceProvider(Provider);
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipBlockProviderIrps(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PWMIP_PROVIDER Provider;

    WmipAcquireExclusive();
    Provider = WmipFindProvider(DeviceObject);
    if (Provider)
        Provider->IrpsBlocked = TRUE;
    WmipRelease();

    return Provider ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

static
VOID
WmipFreeSetList(
    _In_opt_ PWMIP_SET_LIST List)
{
    ULONG Index;

    if (!List)
        return;

    for (Index = 0; Index < List->Count; Index++)
    {
        ExReleaseRundownProtection(&List->Sets[Index]->Provider->Rundown);
        WmipDereferenceSet(List->Sets[Index]);
    }

    ExFreePoolWithTag(List, TAG_WMI_BUFFER);
}

static
BOOLEAN
WmipSetMatches(
    _In_ PWMIP_INSTANCE_SET Set,
    _In_ LPCGUID Guid,
    _In_ BOOLEAN Queryable)
{
    if (!IsEqualGUID(&Set->Guid, Guid))
        return FALSE;
    if (Queryable && (Set->Flags & WMIREG_FLAG_EVENT_ONLY_GUID))
        return FALSE;
    return TRUE;
}

BOOLEAN
WmipIsGuidRegistered(
    _In_ LPCGUID Guid)
{
    PLIST_ENTRY ProviderEntry, SetEntry;
    PWMIP_PROVIDER Provider;
    BOOLEAN Found = FALSE;

    WmipAcquireShared();
    for (ProviderEntry = WmipProviderList.Flink;
         !Found && ProviderEntry != &WmipProviderList;
         ProviderEntry = ProviderEntry->Flink)
    {
        Provider = CONTAINING_RECORD(ProviderEntry, WMIP_PROVIDER, Link);
        for (SetEntry = Provider->InstanceSets.Flink;
             !Found && SetEntry != &Provider->InstanceSets;
             SetEntry = SetEntry->Flink)
        {
            Found = WmipSetMatches(CONTAINING_RECORD(SetEntry, WMIP_INSTANCE_SET, Link), Guid, FALSE);
        }
    }
    WmipRelease();
    return Found;
}

static
PWMIP_SET_LIST
WmipSnapshotSets(
    _In_ LPCGUID Guid,
    _In_ BOOLEAN Queryable)
{
    PLIST_ENTRY ProviderEntry, SetEntry;
    PWMIP_PROVIDER Provider;
    PWMIP_INSTANCE_SET Set;
    PWMIP_SET_LIST List = NULL;
    ULONG Count = 0;

    WmipAcquireShared();
    for (ProviderEntry = WmipProviderList.Flink; ProviderEntry != &WmipProviderList; ProviderEntry = ProviderEntry->Flink)
    {
        Provider = CONTAINING_RECORD(ProviderEntry, WMIP_PROVIDER, Link);
        for (SetEntry = Provider->InstanceSets.Flink; SetEntry != &Provider->InstanceSets; SetEntry = SetEntry->Flink)
        {
            Set = CONTAINING_RECORD(SetEntry, WMIP_INSTANCE_SET, Link);
            if (WmipSetMatches(Set, Guid, Queryable))
                Count++;
        }
    }

    if (Count)
    {
        List = ExAllocatePoolZero(NonPagedPoolNx,
                                  FIELD_OFFSET(WMIP_SET_LIST, Sets[Count]),
                                  TAG_WMI_BUFFER);
    }

    if (List)
    {
        for (ProviderEntry = WmipProviderList.Flink; ProviderEntry != &WmipProviderList; ProviderEntry = ProviderEntry->Flink)
        {
            Provider = CONTAINING_RECORD(ProviderEntry, WMIP_PROVIDER, Link);
            for (SetEntry = Provider->InstanceSets.Flink; SetEntry != &Provider->InstanceSets; SetEntry = SetEntry->Flink)
            {
                Set = CONTAINING_RECORD(SetEntry, WMIP_INSTANCE_SET, Link);
                if (List->Count < Count &&
                    WmipSetMatches(Set, Guid, Queryable) &&
                    ExAcquireRundownProtection(&Provider->Rundown))
                {
                    InterlockedIncrement(&Set->ReferenceCount);
                    List->Sets[List->Count++] = Set;
                }
            }
        }
    }
    WmipRelease();

    if (List && !List->Count)
    {
        ExFreePoolWithTag(List, TAG_WMI_BUFFER);
        List = NULL;
    }

    return List;
}

static
ULONG
WmipNameFlags(
    _In_ PWMIP_INSTANCE_SET Set)
{
    ULONG Flags = 0;

    if (Set->Flags & WMIP_STATIC_NAME_FLAGS)
        Flags |= WNODE_FLAG_STATIC_INSTANCE_NAMES;
    if (Set->Flags & WMIREG_FLAG_INSTANCE_PDO)
        Flags |= WNODE_FLAG_PDO_INSTANCE_NAMES;
    return Flags;
}

static
NTSTATUS
WmipInsertStaticNames(
    _In_ PWMIP_INSTANCE_SET Set,
    _Inout_ PWNODE_ALL_DATA *InOutWnode)
{
    PWNODE_ALL_DATA Wnode = *InOutWnode, NewWnode;
    ULONG Count, Index, OffsetsOffset, NameOffset, Size;
    PULONG Offsets;

    Count = Wnode->InstanceCount;
    if (!(Set->Flags & WMIP_STATIC_NAME_FLAGS) || !Count)
    {
        Wnode->WnodeHeader.Flags &= ~WNODE_FLAG_STATIC_INSTANCE_NAMES;
        return STATUS_SUCCESS;
    }

    if ((Set->Flags & WMIREG_FLAG_INSTANCE_LIST) && Count > Set->InstanceCount)
        return STATUS_INVALID_PARAMETER;

    OffsetsOffset = ALIGN_UP_BY(Wnode->WnodeHeader.BufferSize, sizeof(ULONG));
    Size = OffsetsOffset + Count * sizeof(ULONG);
    for (Index = 0; Index < Count; Index++)
        Size += ALIGN_UP_BY(sizeof(USHORT) + WmipInstanceNameLength(Set, Index), sizeof(USHORT));

    NewWnode = ExAllocatePoolZero(NonPagedPoolNx, Size, TAG_WMI_BUFFER);
    if (!NewWnode)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(NewWnode, Wnode, Wnode->WnodeHeader.BufferSize);
    Offsets = (PULONG)((PUCHAR)NewWnode + OffsetsOffset);
    NameOffset = OffsetsOffset + Count * sizeof(ULONG);
    for (Index = 0; Index < Count; Index++)
    {
        Offsets[Index] = NameOffset;
        WmipWriteInstanceName(Set, Index, (PUCHAR)NewWnode + NameOffset);
        NameOffset += ALIGN_UP_BY(sizeof(USHORT) + WmipInstanceNameLength(Set, Index), sizeof(USHORT));
    }

    NewWnode->OffsetInstanceNameOffsets = OffsetsOffset;
    NewWnode->WnodeHeader.BufferSize = Size;
    NewWnode->WnodeHeader.Flags &= ~WNODE_FLAG_STATIC_INSTANCE_NAMES;

    ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
    *InOutWnode = NewWnode;
    return STATUS_SUCCESS;
}

static
NTSTATUS
WmipQuerySetAllData(
    _In_ PWMIP_INSTANCE_SET Set,
    _Out_ PWNODE_ALL_DATA *OutWnode)
{
    PWNODE_ALL_DATA Wnode;
    ULONG Size = WMIP_INITIAL_REQUEST_SIZE, Needed, Attempt;
    ULONG_PTR Information;
    NTSTATUS Status = STATUS_BUFFER_TOO_SMALL;

    *OutWnode = NULL;

    for (Attempt = 0; Attempt < WMIP_MAX_RETRIES; Attempt++)
    {
        Wnode = ExAllocatePoolZero(NonPagedPoolNx, Size, TAG_WMI_BUFFER);
        if (!Wnode)
            return STATUS_INSUFFICIENT_RESOURCES;

        Wnode->WnodeHeader.BufferSize = sizeof(WNODE_ALL_DATA);
        Wnode->WnodeHeader.ProviderId = Set->Provider->ProviderId;
        Wnode->WnodeHeader.Guid = Set->Guid;
        Wnode->WnodeHeader.Flags = WNODE_FLAG_ALL_DATA | WmipNameFlags(Set);
        KeQuerySystemTime(&Wnode->WnodeHeader.TimeStamp);

        Status = WmipSendIrp(Set->Provider,
                             IRP_MN_QUERY_ALL_DATA,
                             &Set->Guid,
                             Size,
                             Wnode,
                             &Information);
        if ((NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL) &&
            (Wnode->WnodeHeader.Flags & WNODE_FLAG_TOO_SMALL))
        {
            Needed = ((PWNODE_TOO_SMALL)Wnode)->SizeNeeded;
            ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
            if (Needed <= Size || Needed > WMIP_MAX_REQUEST_SIZE)
                return STATUS_INVALID_PARAMETER;
            Size = ALIGN_UP_BY(Needed, sizeof(ULONGLONG));
            continue;
        }

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
            return Status;
        }

        if (Wnode->WnodeHeader.BufferSize < FIELD_OFFSET(WNODE_ALL_DATA, OffsetInstanceDataAndLength) ||
            Wnode->WnodeHeader.BufferSize > Size)
        {
            ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
            return STATUS_INVALID_PARAMETER;
        }

        Wnode->WnodeHeader.Linkage = 0;
        Status = WmipInsertStaticNames(Set, &Wnode);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
            return Status;
        }

        *OutWnode = Wnode;
        return STATUS_SUCCESS;
    }

    return Status;
}

NTSTATUS
NTAPI
WmipQueryAllData(
    _In_ LPCGUID Guid,
    _Inout_ PULONG InOutBufferSize,
    _Out_opt_ PVOID OutBuffer)
{
    PWMIP_SET_LIST List;
    PWNODE_ALL_DATA *Results;
    ULONG Index, Total = 0, Offset, Last = MAXULONG;
    NTSTATUS Status = STATUS_WMI_GUID_NOT_FOUND, SetStatus;

    PAGED_CODE();

    List = WmipSnapshotSets(Guid, TRUE);
    if (!List)
        return STATUS_WMI_GUID_NOT_FOUND;

    Results = ExAllocatePoolZero(PagedPool, List->Count * sizeof(*Results), TAG_WMI_BUFFER);
    if (!Results)
    {
        WmipFreeSetList(List);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Index = 0; Index < List->Count; Index++)
    {
        SetStatus = WmipQuerySetAllData(List->Sets[Index], &Results[Index]);
        if (NT_SUCCESS(SetStatus))
        {
            Total = ALIGN_UP_BY(Total, sizeof(ULONGLONG)) + Results[Index]->WnodeHeader.BufferSize;
            Status = STATUS_SUCCESS;
        }
        else if (Status != STATUS_SUCCESS)
        {
            Status = SetStatus;
        }
    }

    if (NT_SUCCESS(Status))
    {
        if (!OutBuffer || *InOutBufferSize < Total)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            Offset = 0;
            for (Index = 0; Index < List->Count; Index++)
            {
                if (!Results[Index])
                    continue;
                Offset = ALIGN_UP_BY(Offset, sizeof(ULONGLONG));
                if (Last != MAXULONG)
                    ((PWNODE_HEADER)((PUCHAR)OutBuffer + Last))->Linkage = Offset - Last;
                RtlCopyMemory((PUCHAR)OutBuffer + Offset, Results[Index], Results[Index]->WnodeHeader.BufferSize);
                Last = Offset;
                Offset += Results[Index]->WnodeHeader.BufferSize;
            }
        }
        *InOutBufferSize = Total;
    }

    for (Index = 0; Index < List->Count; Index++)
    {
        if (Results[Index])
            ExFreePoolWithTag(Results[Index], TAG_WMI_BUFFER);
    }
    ExFreePoolWithTag(Results, TAG_WMI_BUFFER);
    WmipFreeSetList(List);
    return Status;
}

static
ULONG
WmipInstanceRequestHeaderSize(
    _In_ UCHAR MinorFunction)
{
    switch (MinorFunction)
    {
        case IRP_MN_CHANGE_SINGLE_ITEM:
            return FIELD_OFFSET(WNODE_SINGLE_ITEM, VariableData);
        case IRP_MN_EXECUTE_METHOD:
            return FIELD_OFFSET(WNODE_METHOD_ITEM, VariableData);
        default:
            return FIELD_OFFSET(WNODE_SINGLE_INSTANCE, VariableData);
    }
}

static
NTSTATUS
WmipSendInstanceRequest(
    _In_ PWMIP_INSTANCE_SET Set,
    _In_ BOOLEAN Static,
    _In_ ULONG InstanceIndex,
    _In_ PCUNICODE_STRING InstanceName,
    _In_ UCHAR MinorFunction,
    _In_ ULONG ItemId,
    _In_ ULONG Version,
    _In_ ULONG InSize,
    _In_reads_bytes_opt_(InSize) PVOID InData,
    _In_ ULONG OutCapacity,
    _Out_ PWNODE_HEADER *OutWnode,
    _Out_opt_ PULONG NeededOutput)
{
    ULONG HeaderSize, NameOffset, DataOffset, Size, Needed, Attempt;
    ULONG_PTR Information;
    PWNODE_HEADER Wnode;
    PUCHAR Base;
    USHORT NameLength;
    NTSTATUS Status = STATUS_BUFFER_TOO_SMALL;

    *OutWnode = NULL;

    HeaderSize = WmipInstanceRequestHeaderSize(MinorFunction);
    NameOffset = HeaderSize;
    NameLength = Static ? WmipInstanceNameLength(Set, InstanceIndex) : InstanceName->Length;
    DataOffset = ALIGN_UP_BY(NameOffset + sizeof(USHORT) + NameLength, sizeof(ULONGLONG));
    if (InSize > WMIP_MAX_REQUEST_SIZE || OutCapacity > WMIP_MAX_REQUEST_SIZE)
        return STATUS_INVALID_PARAMETER;
    Size = DataOffset + max(InSize, OutCapacity);
    if (Size < WMIP_INITIAL_REQUEST_SIZE)
        Size = WMIP_INITIAL_REQUEST_SIZE;

    for (Attempt = 0; Attempt < WMIP_MAX_RETRIES; Attempt++)
    {
        Wnode = ExAllocatePoolZero(NonPagedPoolNx, Size, TAG_WMI_BUFFER);
        if (!Wnode)
            return STATUS_INSUFFICIENT_RESOURCES;

        Base = (PUCHAR)Wnode;
        Wnode->BufferSize = DataOffset + InSize;
        Wnode->ProviderId = Set->Provider->ProviderId;
        Wnode->Version = Version;
        Wnode->Guid = Set->Guid;
        Wnode->Flags = Static ? WmipNameFlags(Set) : 0;
        KeQuerySystemTime(&Wnode->TimeStamp);

        if (Static)
        {
            WmipWriteInstanceName(Set, InstanceIndex, Base + NameOffset);
        }
        else
        {
            RtlCopyMemory(Base + NameOffset, &NameLength, sizeof(USHORT));
            RtlCopyMemory(Base + NameOffset + sizeof(USHORT), InstanceName->Buffer, NameLength);
        }
        if (InSize)
            RtlCopyMemory(Base + DataOffset, InData, InSize);

        switch (MinorFunction)
        {
            case IRP_MN_CHANGE_SINGLE_ITEM:
            {
                PWNODE_SINGLE_ITEM Item = (PWNODE_SINGLE_ITEM)Wnode;
                Wnode->Flags |= WNODE_FLAG_SINGLE_ITEM;
                Item->OffsetInstanceName = NameOffset;
                Item->InstanceIndex = InstanceIndex;
                Item->ItemId = ItemId;
                Item->DataBlockOffset = DataOffset;
                Item->SizeDataItem = InSize;
                break;
            }
            case IRP_MN_EXECUTE_METHOD:
            {
                PWNODE_METHOD_ITEM Method = (PWNODE_METHOD_ITEM)Wnode;
                Wnode->Flags |= WNODE_FLAG_METHOD_ITEM;
                Method->OffsetInstanceName = NameOffset;
                Method->InstanceIndex = InstanceIndex;
                Method->MethodId = ItemId;
                Method->DataBlockOffset = DataOffset;
                Method->SizeDataBlock = InSize;
                break;
            }
            default:
            {
                PWNODE_SINGLE_INSTANCE Instance = (PWNODE_SINGLE_INSTANCE)Wnode;
                Wnode->Flags |= WNODE_FLAG_SINGLE_INSTANCE;
                Instance->OffsetInstanceName = NameOffset;
                Instance->InstanceIndex = InstanceIndex;
                Instance->DataBlockOffset = DataOffset;
                Instance->SizeDataBlock = InSize;
                break;
            }
        }

        Status = WmipSendIrp(Set->Provider, MinorFunction, &Set->Guid, Size, Wnode, &Information);
        if ((NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL) &&
            (Wnode->Flags & WNODE_FLAG_TOO_SMALL))
        {
            Needed = ((PWNODE_TOO_SMALL)Wnode)->SizeNeeded;
            ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
            if (Needed <= Size || Needed > WMIP_MAX_REQUEST_SIZE)
                return STATUS_INVALID_PARAMETER;
            if (NeededOutput)
            {
                *NeededOutput = Needed > DataOffset ? Needed - DataOffset : 0;
                return STATUS_BUFFER_TOO_SMALL;
            }
            Size = ALIGN_UP_BY(Needed, sizeof(ULONGLONG));
            continue;
        }

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
            return Status;
        }

        if (Wnode->BufferSize > Size)
        {
            ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
            return STATUS_INVALID_PARAMETER;
        }

        Wnode->Flags &= ~WNODE_FLAG_STATIC_INSTANCE_NAMES;
        *OutWnode = Wnode;
        return STATUS_SUCCESS;
    }

    return Status;
}

static
NTSTATUS
WmipInstanceRequest(
    _In_ LPCGUID Guid,
    _In_ PCUNICODE_STRING InstanceName,
    _In_ UCHAR MinorFunction,
    _In_ ULONG ItemId,
    _In_ ULONG Version,
    _In_ ULONG InSize,
    _In_reads_bytes_opt_(InSize) PVOID InData,
    _In_ ULONG OutCapacity,
    _Out_ PWNODE_HEADER *OutWnode,
    _Out_opt_ PULONG NeededOutput)
{
    PWMIP_SET_LIST List;
    PWMIP_INSTANCE_SET Set;
    ULONG Index, InstanceIndex;
    NTSTATUS Status = STATUS_WMI_INSTANCE_NOT_FOUND;

    PAGED_CODE();

    *OutWnode = NULL;
    if (!InstanceName || !InstanceName->Length)
        return STATUS_WMI_INSTANCE_NOT_FOUND;

    List = WmipSnapshotSets(Guid, TRUE);
    if (!List)
        return STATUS_WMI_GUID_NOT_FOUND;

    for (Index = 0; Index < List->Count; Index++)
    {
        Set = List->Sets[Index];
        if (!(Set->Flags & WMIP_STATIC_NAME_FLAGS))
            continue;
        if (WmipMatchStaticName(Set, InstanceName, &InstanceIndex))
        {
            Status = WmipSendInstanceRequest(Set, TRUE, InstanceIndex, InstanceName, MinorFunction,
                                             ItemId, Version, InSize, InData, OutCapacity,
                                             OutWnode, NeededOutput);
            WmipFreeSetList(List);
            return Status;
        }
    }

    for (Index = 0; Index < List->Count; Index++)
    {
        Set = List->Sets[Index];
        if (Set->Flags & WMIP_STATIC_NAME_FLAGS)
            continue;
        Status = WmipSendInstanceRequest(Set, FALSE, 0, InstanceName, MinorFunction,
                                         ItemId, Version, InSize, InData, OutCapacity,
                                         OutWnode, NeededOutput);
        if (Status != STATUS_WMI_INSTANCE_NOT_FOUND && Status != STATUS_WMI_GUID_NOT_FOUND)
            break;
        Status = STATUS_WMI_INSTANCE_NOT_FOUND;
    }

    WmipFreeSetList(List);
    return Status;
}

NTSTATUS
NTAPI
WmipQuerySingleInstance(
    _In_ LPCGUID Guid,
    _In_ PCUNICODE_STRING InstanceName,
    _Inout_ PULONG InOutBufferSize,
    _Out_opt_ PVOID OutBuffer)
{
    PWNODE_HEADER Wnode;
    NTSTATUS Status;

    Status = WmipInstanceRequest(Guid, InstanceName, IRP_MN_QUERY_SINGLE_INSTANCE,
                                 0, 0, 0, NULL, 0, &Wnode, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!OutBuffer || *InOutBufferSize < Wnode->BufferSize)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        Wnode->Linkage = 0;
        RtlCopyMemory(OutBuffer, Wnode, Wnode->BufferSize);
    }

    *InOutBufferSize = Wnode->BufferSize;
    ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
    return Status;
}

NTSTATUS
NTAPI
WmipSetInstance(
    _In_ LPCGUID Guid,
    _In_ PCUNICODE_STRING InstanceName,
    _In_ BOOLEAN SingleItem,
    _In_ ULONG DataItemId,
    _In_ ULONG Version,
    _In_ ULONG ValueBufferSize,
    _In_reads_bytes_(ValueBufferSize) PVOID ValueBuffer)
{
    PWNODE_HEADER Wnode;
    NTSTATUS Status;

    Status = WmipInstanceRequest(Guid,
                                 InstanceName,
                                 SingleItem ? IRP_MN_CHANGE_SINGLE_ITEM : IRP_MN_CHANGE_SINGLE_INSTANCE,
                                 DataItemId,
                                 Version,
                                 ValueBufferSize,
                                 ValueBuffer,
                                 0,
                                 &Wnode,
                                 NULL);
    if (NT_SUCCESS(Status))
        ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
    return Status;
}

NTSTATUS
NTAPI
WmipExecuteMethod(
    _In_ LPCGUID Guid,
    _In_ PCUNICODE_STRING InstanceName,
    _In_ ULONG MethodId,
    _In_ ULONG InBufferSize,
    _Inout_ PULONG OutBufferSize,
    _Inout_ PUCHAR InOutBuffer)
{
    PWNODE_METHOD_ITEM Method;
    PWNODE_HEADER Wnode;
    ULONG Needed = 0;
    NTSTATUS Status;

    Status = WmipInstanceRequest(Guid, InstanceName, IRP_MN_EXECUTE_METHOD, MethodId, 0,
                                 InBufferSize, InOutBuffer, *OutBufferSize, &Wnode, &Needed);
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        *OutBufferSize = Needed;
        return Status;
    }
    if (!NT_SUCCESS(Status))
        return Status;

    Method = (PWNODE_METHOD_ITEM)Wnode;
    if (Method->DataBlockOffset > Wnode->BufferSize ||
        Method->SizeDataBlock > Wnode->BufferSize - Method->DataBlockOffset)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else if (Method->SizeDataBlock > *OutBufferSize)
    {
        *OutBufferSize = Method->SizeDataBlock;
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        if (Method->SizeDataBlock)
            RtlCopyMemory(InOutBuffer, (PUCHAR)Wnode + Method->DataBlockOffset, Method->SizeDataBlock);
        *OutBufferSize = Method->SizeDataBlock;
    }

    ExFreePoolWithTag(Wnode, TAG_WMI_BUFFER);
    return Status;
}

static
BOOLEAN
WmipSameDeviceStack(
    _In_ PDEVICE_OBJECT First,
    _In_ PDEVICE_OBJECT Second)
{
    PDEVICE_OBJECT FirstBase, SecondBase;
    BOOLEAN Same;

    if (First == Second)
        return TRUE;

    FirstBase = IoGetDeviceAttachmentBaseRef(First);
    SecondBase = IoGetDeviceAttachmentBaseRef(Second);
    Same = (FirstBase == SecondBase);
    ObDereferenceObject(FirstBase);
    ObDereferenceObject(SecondBase);
    return Same;
}

NTSTATUS
NTAPI
WmipDeviceObjectToInstanceName(
    _In_ LPCGUID Guid,
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PUNICODE_STRING InstanceName)
{
    PWMIP_SET_LIST List;
    PWMIP_INSTANCE_SET Set;
    ULONG Index;
    NTSTATUS Status = STATUS_WMI_INSTANCE_NOT_FOUND;

    PAGED_CODE();

    List = WmipSnapshotSets(Guid, FALSE);
    if (!List)
        return STATUS_WMI_GUID_NOT_FOUND;

    for (Index = 0; Index < List->Count; Index++)
    {
        Set = List->Sets[Index];
        if ((Set->Flags & WMIP_STATIC_NAME_FLAGS) && Set->InstanceCount &&
            WmipSameDeviceStack(Set->Provider->DeviceObject, DeviceObject))
        {
            Status = WmipAllocateInstanceName(Set, 0, InstanceName);
            break;
        }
    }

    WmipFreeSetList(List);
    return Status;
}

NTSTATUS
NTAPI
WmipAllocateInstanceIds(
    _In_ LPCGUID Guid,
    _In_ ULONG InstanceCount,
    _Out_ PULONG FirstInstanceId)
{
    PWMIP_GUID_STATE State;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    WmipAcquireExclusive();
    State = WmipFindGuidState(Guid, TRUE);
    if (!State)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else if (InstanceCount > MAXULONG - State->NextInstanceId)
    {
        Status = STATUS_UNSUCCESSFUL;
    }
    else
    {
        *FirstInstanceId = State->NextInstanceId;
        State->NextInstanceId += InstanceCount;
    }
    WmipRelease();

    return Status;
}

typedef struct _WMIP_CONTROL_ACTION
{
    PWMIP_INSTANCE_SET Set;
    UCHAR MinorFunction;
} WMIP_CONTROL_ACTION, *PWMIP_CONTROL_ACTION;

static
VOID
WmipUpdateGuidControl(
    _In_ LPCGUID Guid)
{
    PLIST_ENTRY ProviderEntry, SetEntry;
    PWMIP_PROVIDER Provider;
    PWMIP_INSTANCE_SET Set;
    PWMIP_GUID_STATE State;
    PWMIP_CONTROL_ACTION Actions = NULL;
    ULONG Count = 0, Used = 0, Index;
    BOOLEAN WantEvents, WantCollection;
    WNODE_HEADER Header;
    ULONG_PTR Information;

    PAGED_CODE();

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&WmipControlResource, TRUE);

    WmipAcquireShared();
    State = WmipFindGuidState(Guid, FALSE);
    WantEvents = State && State->EventCount;
    WantCollection = State && State->OpenCount;

    for (ProviderEntry = WmipProviderList.Flink; ProviderEntry != &WmipProviderList; ProviderEntry = ProviderEntry->Flink)
    {
        Provider = CONTAINING_RECORD(ProviderEntry, WMIP_PROVIDER, Link);
        for (SetEntry = Provider->InstanceSets.Flink; SetEntry != &Provider->InstanceSets; SetEntry = SetEntry->Flink)
        {
            Set = CONTAINING_RECORD(SetEntry, WMIP_INSTANCE_SET, Link);
            if (IsEqualGUID(&Set->Guid, Guid))
                Count += 2;
        }
    }

    if (Count)
        Actions = ExAllocatePoolZero(NonPagedPoolNx, Count * sizeof(*Actions), TAG_WMI_BUFFER);

    if (Actions)
    {
        for (ProviderEntry = WmipProviderList.Flink; ProviderEntry != &WmipProviderList; ProviderEntry = ProviderEntry->Flink)
        {
            Provider = CONTAINING_RECORD(ProviderEntry, WMIP_PROVIDER, Link);
            for (SetEntry = Provider->InstanceSets.Flink; SetEntry != &Provider->InstanceSets; SetEntry = SetEntry->Flink)
            {
                Set = CONTAINING_RECORD(SetEntry, WMIP_INSTANCE_SET, Link);
                if (!IsEqualGUID(&Set->Guid, Guid))
                    continue;

                if ((Set->Flags & WMIREG_FLAG_EXPENSIVE) &&
                    !(Set->Flags & WMIREG_FLAG_EVENT_ONLY_GUID) &&
                    Set->CollectionEnabled != WantCollection &&
                    Used < Count &&
                    ExAcquireRundownProtection(&Provider->Rundown))
                {
                    Set->CollectionEnabled = WantCollection;
                    InterlockedIncrement(&Set->ReferenceCount);
                    Actions[Used].Set = Set;
                    Actions[Used].MinorFunction = WantCollection ? IRP_MN_ENABLE_COLLECTION : IRP_MN_DISABLE_COLLECTION;
                    Used++;
                }

                if (Set->EventsEnabled != WantEvents &&
                    Used < Count &&
                    ExAcquireRundownProtection(&Provider->Rundown))
                {
                    Set->EventsEnabled = WantEvents;
                    InterlockedIncrement(&Set->ReferenceCount);
                    Actions[Used].Set = Set;
                    Actions[Used].MinorFunction = WantEvents ? IRP_MN_ENABLE_EVENTS : IRP_MN_DISABLE_EVENTS;
                    Used++;
                }
            }
        }
    }
    WmipRelease();

    for (Index = 0; Index < Used; Index++)
    {
        Set = Actions[Index].Set;
        RtlZeroMemory(&Header, sizeof(Header));
        Header.BufferSize = sizeof(Header);
        Header.ProviderId = Set->Provider->ProviderId;
        Header.Guid = Set->Guid;
        KeQuerySystemTime(&Header.TimeStamp);
        WmipSendIrp(Set->Provider, Actions[Index].MinorFunction, &Set->Guid,
                    sizeof(Header), &Header, &Information);
        ExReleaseRundownProtection(&Set->Provider->Rundown);
        WmipDereferenceSet(Set);
    }

    if (Actions)
        ExFreePoolWithTag(Actions, TAG_WMI_BUFFER);

    ExReleaseResourceLite(&WmipControlResource);
    KeLeaveCriticalRegion();
}

static
VOID
WmipAdjustGuidCount(
    _In_ LPCGUID Guid,
    _In_ BOOLEAN Events,
    _In_ LONG Delta)
{
    PWMIP_GUID_STATE State;
    PULONG Counter;
    BOOLEAN Changed = FALSE;

    WmipAcquireExclusive();
    State = WmipFindGuidState(Guid, Delta > 0);
    if (State)
    {
        Counter = Events ? &State->EventCount : &State->OpenCount;
        if (Delta > 0)
        {
            Changed = (*Counter)++ == 0;
        }
        else if (*Counter)
        {
            Changed = --(*Counter) == 0;
        }
        WmipReleaseGuidState(State);
    }
    WmipRelease();

    if (Changed)
        WmipUpdateGuidControl(Guid);
}

VOID
NTAPI
WmipGuidObjectCreated(
    _In_ LPCGUID Guid)
{
    WmipAdjustGuidCount(Guid, FALSE, 1);
}

VOID
NTAPI
WmipGuidObjectDeleted(
    _In_ PWMIP_GUID_OBJECT GuidObject)
{
    if (GuidObject->EventConsumer)
    {
        GuidObject->EventConsumer = FALSE;
        WmipAdjustGuidCount(&GuidObject->Guid, TRUE, -1);
    }
    WmipAdjustGuidCount(&GuidObject->Guid, FALSE, -1);
}

VOID
NTAPI
WmipSetEventConsumer(
    _In_ PWMIP_GUID_OBJECT GuidObject,
    _In_ BOOLEAN Consumer)
{
    if (GuidObject->EventConsumer == Consumer)
        return;

    GuidObject->EventConsumer = Consumer;
    WmipAdjustGuidCount(&GuidObject->Guid, TRUE, Consumer ? 1 : -1);
}

PWNODE_HEADER
NTAPI
WmipNameEventInstance(
    _In_ PWNODE_HEADER Wnode)
{
    PLIST_ENTRY ProviderEntry, SetEntry;
    PWMIP_PROVIDER Provider;
    PWMIP_INSTANCE_SET Set, Found = NULL;
    PWNODE_SINGLE_INSTANCE Instance, NewInstance;
    PWNODE_SINGLE_ITEM Item, NewItem;
    ULONG HeaderSize, Index, DataOffset, DataSize, NewSize, NameLength;
    PWNODE_HEADER NewWnode = NULL;

    if (!(Wnode->Flags & WNODE_FLAG_STATIC_INSTANCE_NAMES))
        return Wnode;

    if (Wnode->Flags & WNODE_FLAG_SINGLE_INSTANCE)
    {
        Instance = (PWNODE_SINGLE_INSTANCE)Wnode;
        HeaderSize = FIELD_OFFSET(WNODE_SINGLE_INSTANCE, VariableData);
        Index = Instance->InstanceIndex;
        DataOffset = Instance->DataBlockOffset;
        DataSize = Instance->SizeDataBlock;
    }
    else if (Wnode->Flags & WNODE_FLAG_SINGLE_ITEM)
    {
        Item = (PWNODE_SINGLE_ITEM)Wnode;
        HeaderSize = FIELD_OFFSET(WNODE_SINGLE_ITEM, VariableData);
        Index = Item->InstanceIndex;
        DataOffset = Item->DataBlockOffset;
        DataSize = Item->SizeDataItem;
    }
    else
    {
        return Wnode;
    }

    if (Wnode->BufferSize < HeaderSize || DataOffset > Wnode->BufferSize ||
        DataSize > Wnode->BufferSize - DataOffset)
    {
        return Wnode;
    }

    WmipAcquireShared();
    for (ProviderEntry = WmipProviderList.Flink; !Found && ProviderEntry != &WmipProviderList; ProviderEntry = ProviderEntry->Flink)
    {
        Provider = CONTAINING_RECORD(ProviderEntry, WMIP_PROVIDER, Link);
        if (Provider->ProviderId != Wnode->ProviderId)
            continue;
        for (SetEntry = Provider->InstanceSets.Flink; SetEntry != &Provider->InstanceSets; SetEntry = SetEntry->Flink)
        {
            Set = CONTAINING_RECORD(SetEntry, WMIP_INSTANCE_SET, Link);
            if (IsEqualGUID(&Set->Guid, &Wnode->Guid) &&
                (Set->Flags & WMIP_STATIC_NAME_FLAGS) &&
                Index < Set->InstanceCount)
            {
                Found = Set;
                break;
            }
        }
    }

    if (Found)
    {
        NameLength = WmipInstanceNameLength(Found, Index);
        NewSize = ALIGN_UP_BY(HeaderSize + sizeof(USHORT) + NameLength, sizeof(ULONGLONG)) + DataSize;
        NewWnode = ExAllocatePoolZero(NonPagedPoolNx, NewSize, TAG_WMI_BUFFER);
        if (NewWnode)
        {
            RtlCopyMemory(NewWnode, Wnode, HeaderSize);
            WmipWriteInstanceName(Found, Index, (PUCHAR)NewWnode + HeaderSize);
            RtlCopyMemory((PUCHAR)NewWnode + NewSize - DataSize, (PUCHAR)Wnode + DataOffset, DataSize);
            NewWnode->BufferSize = NewSize;
            NewWnode->Flags &= ~WNODE_FLAG_STATIC_INSTANCE_NAMES;
            if (NewWnode->Flags & WNODE_FLAG_SINGLE_INSTANCE)
            {
                NewInstance = (PWNODE_SINGLE_INSTANCE)NewWnode;
                NewInstance->OffsetInstanceName = HeaderSize;
                NewInstance->DataBlockOffset = NewSize - DataSize;
            }
            else
            {
                NewItem = (PWNODE_SINGLE_ITEM)NewWnode;
                NewItem->OffsetInstanceName = HeaderSize;
                NewItem->DataBlockOffset = NewSize - DataSize;
            }
        }
    }
    WmipRelease();

    if (!NewWnode)
        return Wnode;

    ExFreePool(Wnode);
    return NewWnode;
}

VOID
NTAPI
WmipInitializeProviders(
    VOID)
{
    ExInitializeResourceLite(&WmipRegistrationResource);
    ExInitializeResourceLite(&WmipControlResource);
    InitializeListHead(&WmipProviderList);
    InitializeListHead(&WmipGuidStateList);
    InitializeListHead(&WmipRegistrationQueue);
    KeInitializeSpinLock(&WmipQueueLock);
    KeInitializeSpinLock(&WmipProviderLock);
    ExInitializeWorkItem(&WmipRegistrationWorkItem, WmipRegistrationWorker, NULL);
}

NTSTATUS
NTAPI
IoWMIRegistrationControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG Action)
{
    PAGED_CODE();

    if (!DeviceObject)
        return STATUS_INVALID_PARAMETER;

    switch (Action)
    {
        case WMIREG_ACTION_REGISTER:
            return WmipRegisterProvider(DeviceObject);

        case WMIREG_ACTION_DEREGISTER:
            return WmipDeregisterProvider(DeviceObject);

        case WMIREG_ACTION_REREGISTER:
            WmipDeregisterProvider(DeviceObject);
            return WmipRegisterProvider(DeviceObject);

        case WMIREG_ACTION_UPDATE_GUIDS:
            return WmipQueueProviderAction(DeviceObject, WMIP_PENDING_UPDATE);

        case WMIREG_ACTION_BLOCK_IRPS:
            return WmipBlockProviderIrps(DeviceObject);

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

#if defined(_WIN64)
ULONG
NTAPI
IoWMIDeviceObjectToProviderId(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PWMIP_PROVIDER Provider;
    ULONG ProviderId = 0;
    KIRQL OldIrql;

    KeAcquireSpinLock(&WmipProviderLock, &OldIrql);
    Provider = WmipFindProvider(DeviceObject);
    if (Provider)
        ProviderId = Provider->ProviderId;
    KeReleaseSpinLock(&WmipProviderLock, OldIrql);

    return ProviderId;
}
#endif
