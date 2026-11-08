/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Event tracing provider registrations and trace GUID queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <wmistr.h>
#include <ntwmi.h>
#include "wmip.h"

#define NDEBUG
#include <debug.h>

#define ETWP_REG_TAG 'RwtE'

typedef struct _ETWP_REGISTRATION
{
    LIST_ENTRY Link;
    GUID Guid;
    ULONG ProcessId;
    ULONG Flags;
} ETWP_REGISTRATION, *PETWP_REGISTRATION;

static POBJECT_TYPE EtwpRegistrationObjectType;
static EX_PUSH_LOCK EtwpRegistrationLock;
static LIST_ENTRY EtwpRegistrationList;

static
VOID
NTAPI
EtwpDeleteRegistration(
    _In_ PVOID Object)
{
    PETWP_REGISTRATION Registration = Object;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&EtwpRegistrationLock);
    RemoveEntryList(&Registration->Link);
    ExReleasePushLockExclusive(&EtwpRegistrationLock);
    KeLeaveCriticalRegion();
}

NTSTATUS
NTAPI
EtwpInitializeRegistrations(VOID)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"EtwRegistration");
    OBJECT_TYPE_INITIALIZER Initializer;

    ExInitializePushLock(&EtwpRegistrationLock);
    InitializeListHead(&EtwpRegistrationList);

    RtlZeroMemory(&Initializer, sizeof(Initializer));
    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.GenericMapping = WmipGenericMapping;
    Initializer.ValidAccessMask = WMIGUID_ALL_ACCESS;
    Initializer.PoolType = NonPagedPoolNx;
    Initializer.DefaultNonPagedPoolCharge = sizeof(ETWP_REGISTRATION);
    Initializer.DeleteProcedure = EtwpDeleteRegistration;

    return ObCreateObjectType(&Name, &Initializer, NULL, &EtwpRegistrationObjectType);
}

NTSTATUS
EtwpRegisterGuid(
    _In_reads_bytes_opt_(InBufferLen) PVOID InBuffer,
    _In_ ULONG InBufferLen,
    _Out_writes_bytes_opt_(OutBufferLen) PVOID OutBuffer,
    _In_ ULONG OutBufferLen,
    _Out_opt_ PULONG ReturnSize,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ETW_REGISTER_GUID_INFO Info;
    PETWP_REGISTRATION Registration;
    HANDLE Handle;
    NTSTATUS Status;

    if (!InBuffer || InBufferLen != sizeof(Info) || !OutBuffer || OutBufferLen < sizeof(ULONG64))
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(InBuffer, sizeof(Info), sizeof(ULONG));
            ProbeForWrite(OutBuffer, sizeof(ULONG64), sizeof(ULONG));
            if (ReturnSize)
                ProbeForWriteUlong(ReturnSize);
        }
        RtlCopyMemory(&Info, InBuffer, sizeof(Info));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Info.Flags & ~ETW_REGISTRATION_FLAG_LEGACY)
        return STATUS_INVALID_PARAMETER;

    Status = ObCreateObject(PreviousMode,
                            EtwpRegistrationObjectType,
                            NULL,
                            PreviousMode,
                            NULL,
                            sizeof(ETWP_REGISTRATION),
                            0,
                            0,
                            (PVOID *)&Registration);
    if (!NT_SUCCESS(Status))
        return Status;

    Registration->Guid = Info.Guid;
    Registration->ProcessId = HandleToUlong(PsGetCurrentProcessId());
    Registration->Flags = Info.Flags;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&EtwpRegistrationLock);
    InsertTailList(&EtwpRegistrationList, &Registration->Link);
    ExReleasePushLockExclusive(&EtwpRegistrationLock);
    KeLeaveCriticalRegion();

    Status = ObInsertObject(Registration, NULL, WMIGUID_ALL_ACCESS, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        *(PULONG64)OutBuffer = (ULONG64)(ULONG_PTR)Handle;
        if (ReturnSize)
            *ReturnSize = sizeof(ULONG64);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
        ObCloseHandle(Handle, PreviousMode);

    return Status;
}

static
NTSTATUS
EtwpReturnBuffer(
    _In_reads_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size,
    _Out_writes_bytes_opt_(OutBufferLen) PVOID OutBuffer,
    _In_ ULONG OutBufferLen,
    _Out_opt_ PULONG ReturnSize,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Size > OutBufferLen || (Size && !OutBuffer))
        Status = STATUS_BUFFER_TOO_SMALL;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            if (ReturnSize)
                ProbeForWriteUlong(ReturnSize);
            if (NT_SUCCESS(Status) && Size)
                ProbeForWrite(OutBuffer, Size, sizeof(ULONG));
        }
        if (NT_SUCCESS(Status) && Size)
            RtlCopyMemory(OutBuffer, Buffer, Size);
        if (ReturnSize)
            *ReturnSize = Size;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
EtwpEnumTraceGuidList(
    _In_reads_bytes_opt_(InBufferLen) PVOID InBuffer,
    _In_ ULONG InBufferLen,
    _Out_writes_bytes_opt_(OutBufferLen) PVOID OutBuffer,
    _In_ ULONG OutBufferLen,
    _Out_opt_ PULONG ReturnSize,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PETWP_REGISTRATION Registration;
    PLIST_ENTRY Entry;
    GUID *Guids = NULL;
    ULONG ProcessId = 0;
    ULONG Flags = 0;
    ULONG Count = 0;
    ULONG Index;
    NTSTATUS Status;

    if (InBuffer)
    {
        if (InBufferLen != sizeof(ULONG))
            return STATUS_INVALID_PARAMETER;
        _SEH2_TRY
        {
            if (PreviousMode != KernelMode)
                ProbeForRead(InBuffer, sizeof(ULONG), sizeof(ULONG));
            Flags = *(PULONG)InBuffer;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else if (InBufferLen)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Flags & ~ETW_ENUM_GUIDS_CURRENT_PROCESS)
        return STATUS_INVALID_PARAMETER;
    if (Flags & ETW_ENUM_GUIDS_CURRENT_PROCESS)
        ProcessId = HandleToUlong(PsGetCurrentProcessId());

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&EtwpRegistrationLock);

    for (Entry = EtwpRegistrationList.Flink; Entry != &EtwpRegistrationList; Entry = Entry->Flink)
        Count++;

    if (Count)
    {
        Guids = ExAllocatePoolWithTag(PagedPool, Count * sizeof(GUID), ETWP_REG_TAG);
        if (!Guids)
        {
            ExReleasePushLockShared(&EtwpRegistrationLock);
            KeLeaveCriticalRegion();
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    Count = 0;
    for (Entry = EtwpRegistrationList.Flink; Entry != &EtwpRegistrationList; Entry = Entry->Flink)
    {
        Registration = CONTAINING_RECORD(Entry, ETWP_REGISTRATION, Link);
        if (ProcessId && Registration->ProcessId != ProcessId)
            continue;
        for (Index = 0; Index < Count; Index++)
        {
            if (IsEqualGUID(&Guids[Index], &Registration->Guid))
                break;
        }
        if (Index == Count)
            Guids[Count++] = Registration->Guid;
    }

    ExReleasePushLockShared(&EtwpRegistrationLock);
    KeLeaveCriticalRegion();

    Status = EtwpReturnBuffer(Guids, Count * sizeof(GUID), OutBuffer, OutBufferLen, ReturnSize, PreviousMode);
    if (Guids)
        ExFreePoolWithTag(Guids, ETWP_REG_TAG);
    return Status;
}

NTSTATUS
EtwpGetTraceGuidInfo(
    _In_reads_bytes_opt_(InBufferLen) PVOID InBuffer,
    _In_ ULONG InBufferLen,
    _Out_writes_bytes_opt_(OutBufferLen) PVOID OutBuffer,
    _In_ ULONG OutBufferLen,
    _Out_opt_ PULONG ReturnSize,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PETWP_REGISTRATION Registration;
    PTRACE_PROVIDER_INSTANCE_INFO Instance;
    PTRACE_GUID_INFO GuidInfo;
    PLIST_ENTRY Entry;
    GUID Guid;
    ULONG Count = 0;
    ULONG Size;
    NTSTATUS Status;

    if (!InBuffer || InBufferLen != sizeof(GUID))
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead(InBuffer, sizeof(GUID), sizeof(ULONG));
        RtlCopyMemory(&Guid, InBuffer, sizeof(GUID));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(&EtwpRegistrationLock);

    for (Entry = EtwpRegistrationList.Flink; Entry != &EtwpRegistrationList; Entry = Entry->Flink)
    {
        Registration = CONTAINING_RECORD(Entry, ETWP_REGISTRATION, Link);
        if (IsEqualGUID(&Registration->Guid, &Guid))
            Count++;
    }

    if (!Count)
    {
        ExReleasePushLockShared(&EtwpRegistrationLock);
        KeLeaveCriticalRegion();
        return STATUS_WMI_GUID_NOT_FOUND;
    }

    Size = sizeof(TRACE_GUID_INFO) + Count * sizeof(TRACE_PROVIDER_INSTANCE_INFO);
    GuidInfo = ExAllocatePoolZero(PagedPool, Size, ETWP_REG_TAG);
    if (!GuidInfo)
    {
        ExReleasePushLockShared(&EtwpRegistrationLock);
        KeLeaveCriticalRegion();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    GuidInfo->InstanceCount = Count;
    Instance = (PTRACE_PROVIDER_INSTANCE_INFO)(GuidInfo + 1);
    for (Entry = EtwpRegistrationList.Flink; Entry != &EtwpRegistrationList; Entry = Entry->Flink)
    {
        Registration = CONTAINING_RECORD(Entry, ETWP_REGISTRATION, Link);
        if (!IsEqualGUID(&Registration->Guid, &Guid))
            continue;
        if (--Count)
            Instance->NextOffset = sizeof(TRACE_PROVIDER_INSTANCE_INFO);
        Instance->Pid = Registration->ProcessId;
        if (Registration->Flags & ETW_REGISTRATION_FLAG_LEGACY)
            Instance->Flags = TRACE_PROVIDER_FLAG_LEGACY;
        Instance++;
    }

    ExReleasePushLockShared(&EtwpRegistrationLock);
    KeLeaveCriticalRegion();

    Status = EtwpReturnBuffer(GuidInfo, Size, OutBuffer, OutBufferLen, ReturnSize, PreviousMode);
    ExFreePoolWithTag(GuidInfo, ETWP_REG_TAG);
    return Status;
}
