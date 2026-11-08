/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel Shim Engine
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define TAG_KSE 'hseK'

#define KSE_ENGINE_INITIALIZED 2
#define KSE_REGISTRATION_UNREGISTER_PENDING 0x00000004

typedef struct _KSE_SHIM_REGISTRATION
{
    LIST_ENTRY Link;
    PKSE_SHIM Shim;
    LONG RefCount;
    ULONG Flags;
    PVOID Context;
} KSE_SHIM_REGISTRATION, *PKSE_SHIM_REGISTRATION;

typedef struct _KSE_ENGINE
{
    ULONG Flags;
    volatile LONG State;
    LIST_ENTRY ShimList;
    EX_PUSH_LOCK Lock;
    KSE_CALLBACK_ROUTINES Callbacks;
} KSE_ENGINE;

typedef struct _KSE_COMPLETION_HOOK
{
    PDEVICE_OBJECT DeviceObject;
    PIO_COMPLETION_ROUTINE HookRoutine;
    PVOID HookContext;
    PIO_COMPLETION_ROUTINE OriginalRoutine;
    PVOID OriginalContext;
    ULONG OriginalControl;
} KSE_COMPLETION_HOOK, *PKSE_COMPLETION_HOOK;

static KSE_ENGINE KseEngine;

static const WCHAR KsepCompatibilityKey[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Compatibility";
static const WCHAR KsepDeviceKey[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Compatibility\\Device";
static const WCHAR KsepPolicyKey[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Policies\\Microsoft\\Compatibility";

static
BOOLEAN
KsepIsEngineReady(
    _In_ ULONG DisabledFlag)
{
    return (ReadAcquire(&KseEngine.State) == KSE_ENGINE_INITIALIZED) &&
           !(KseEngine.Flags & DisabledFlag);
}

static
NTSTATUS
KsepOpenKey(
    _In_ PCWSTR Path,
    _In_opt_ PCUNICODE_STRING SubKey,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE KeyHandle)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE ParentHandle;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!SubKey)
    {
        if (Create)
            return ZwCreateKey(KeyHandle, DesiredAccess, &ObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
        return ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
    }

    if (Create)
        Status = ZwCreateKey(&ParentHandle, KEY_CREATE_SUB_KEY, &ObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
    else
        Status = ZwOpenKey(&ParentHandle, KEY_ENUMERATE_SUB_KEYS, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)SubKey, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, ParentHandle, NULL);
    if (Create)
        Status = ZwCreateKey(KeyHandle, DesiredAccess, &ObjectAttributes, 0, NULL, REG_OPTION_NON_VOLATILE, NULL);
    else
        Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
    ZwClose(ParentHandle);
    return Status;
}

static
NTSTATUS
KsepQueryDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _Out_ PULONG Value)
{
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    UNICODE_STRING Name;
    ULONG ResultLength;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, ValueName);
    Status = ZwQueryValueKey(KeyHandle, &Name, KeyValuePartialInformation, Info, sizeof(Buffer), &ResultLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Info->Type != REG_DWORD || Info->DataLength != sizeof(ULONG))
        return STATUS_OBJECT_TYPE_MISMATCH;

    *Value = *(PULONG)Info->Data;
    return STATUS_SUCCESS;
}

static
NTSTATUS
KsepTransformDeviceId(
    _In_ PCWSTR DeviceId,
    _Out_ PUNICODE_STRING Transformed)
{
    UNICODE_STRING Source;
    USHORT i;

    RtlInitUnicodeString(&Source, DeviceId);
    Transformed->Length = Source.Length;
    Transformed->MaximumLength = Source.Length + sizeof(WCHAR);
    Transformed->Buffer = ExAllocatePoolWithTag(PagedPool, Transformed->MaximumLength, TAG_KSE);
    if (!Transformed->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (i = 0; i < Source.Length / sizeof(WCHAR); i++)
        Transformed->Buffer[i] = (Source.Buffer[i] == L'\\') ? L'!' : Source.Buffer[i];
    Transformed->Buffer[i] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
NTSTATUS
KsepOpenDeviceKey(
    _In_ PCWSTR DeviceId,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE KeyHandle)
{
    UNICODE_STRING SubKey;
    UNICODE_STRING DeviceName;
    HANDLE CompatibilityHandle;
    NTSTATUS Status;

    Status = KsepTransformDeviceId(DeviceId, &SubKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = KsepOpenKey(KsepDeviceKey, &SubKey, DesiredAccess, Create, KeyHandle);
    if (Create && Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        RtlInitUnicodeString(&DeviceName, L"Device");
        Status = KsepOpenKey(KsepCompatibilityKey, &DeviceName, KEY_CREATE_SUB_KEY, TRUE, &CompatibilityHandle);
        if (NT_SUCCESS(Status))
        {
            ZwClose(CompatibilityHandle);
            Status = KsepOpenKey(KsepDeviceKey, &SubKey, DesiredAccess, TRUE, KeyHandle);
        }
    }

    ExFreePoolWithTag(SubKey.Buffer, TAG_KSE);
    return Status;
}

static
NTSTATUS
KsepQueryRegistryDeviceData(
    _In_ PCWSTR DeviceId,
    _In_ PCWSTR Provider,
    _Inout_ PULONG Type,
    _Inout_ PULONG Size,
    _Out_writes_bytes_opt_(*Size) PVOID Data)
{
    PKEY_VALUE_PARTIAL_INFORMATION Info;
    UNICODE_STRING ValueName;
    HANDLE KeyHandle;
    ULONG ResultLength;
    NTSTATUS Status;

    Status = KsepOpenDeviceKey(DeviceId, KEY_QUERY_VALUE, FALSE, &KeyHandle);
    if (!NT_SUCCESS(Status))
        return STATUS_NOT_FOUND;

    RtlInitUnicodeString(&ValueName, Provider);
    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, NULL, 0, &ResultLength);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
    {
        ZwClose(KeyHandle);
        return (Status == STATUS_OBJECT_NAME_NOT_FOUND || NT_SUCCESS(Status)) ? STATUS_NOT_FOUND : Status;
    }

    Info = ExAllocatePoolWithTag(PagedPool, ResultLength, TAG_KSE);
    if (!Info)
    {
        ZwClose(KeyHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, Info, ResultLength, &ResultLength);
    ZwClose(KeyHandle);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        Status = STATUS_NOT_FOUND;
    }
    else if (NT_SUCCESS(Status))
    {
        if (Info->DataLength > *Size)
        {
            *Size = Info->DataLength;
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            if (Data)
                RtlCopyMemory(Data, Info->Data, Info->DataLength);
            *Size = Info->DataLength;
            *Type |= Info->Type | KSE_DEVICE_DATA_FROM_REGISTRY;
        }
    }

    ExFreePoolWithTag(Info, TAG_KSE);
    return Status;
}

static
NTSTATUS
KsepQueryRegistryDeviceDataList(
    _In_ PCWSTR DeviceId,
    _Out_writes_bytes_opt_(BufferSize) PWSTR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG RequiredSize)
{
    PKEY_VALUE_BASIC_INFORMATION Info;
    KEY_FULL_INFORMATION KeyInfo;
    HANDLE KeyHandle;
    ULONG InfoSize, ResultLength, Index, Total = 0;
    PUCHAR Out = (PUCHAR)Buffer;
    NTSTATUS Status;

    Status = KsepOpenDeviceKey(DeviceId, KEY_QUERY_VALUE, FALSE, &KeyHandle);
    if (!NT_SUCCESS(Status))
        return STATUS_NOT_FOUND;

    Status = ZwQueryKey(KeyHandle, KeyFullInformation, &KeyInfo, sizeof(KeyInfo), &ResultLength);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
    {
        ZwClose(KeyHandle);
        return Status;
    }

    InfoSize = FIELD_OFFSET(KEY_VALUE_BASIC_INFORMATION, Name) + KeyInfo.MaxValueNameLen + sizeof(WCHAR);
    Info = ExAllocatePoolWithTag(PagedPool, InfoSize, TAG_KSE);
    if (!Info)
    {
        ZwClose(KeyHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Index = 0; ; Index++)
    {
        ULONG Length;

        Status = ZwEnumerateValueKey(KeyHandle, Index, KeyValueBasicInformation, Info, InfoSize, &ResultLength);
        if (Status == STATUS_NO_MORE_ENTRIES)
            break;
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        Length = Info->NameLength + sizeof(WCHAR);
        Total += Length;
        if (Out && Total <= BufferSize)
        {
            RtlCopyMemory(Out, Info->Name, Info->NameLength);
            ((PWCHAR)(Out + Info->NameLength))[0] = UNICODE_NULL;
            Out += Length;
        }
    }

    *RequiredSize = Total + sizeof(WCHAR);
    if (Total + sizeof(WCHAR) > BufferSize || !Buffer)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        Buffer[Total / sizeof(WCHAR)] = UNICODE_NULL;
        Status = STATUS_SUCCESS;
    }

Cleanup:
    ExFreePoolWithTag(Info, TAG_KSE);
    ZwClose(KeyHandle);
    return Status;
}

static
PVOID
NTAPI
KsepGetIoCallbacks(
    _In_ PDRIVER_OBJECT DriverObject)
{
    return ((PEXTENDED_DRIVER_EXTENSION)DriverObject->DriverExtension)->KseCallbacks;
}

static
NTSTATUS
NTAPI
KsepCompletionSafeWrapper(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_opt_ PVOID Context)
{
    PKSE_COMPLETION_HOOK Hook = Context;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Invoke;

    if (Hook && Hook->HookRoutine)
        Hook->HookRoutine(Hook->DeviceObject, Irp, Hook->HookContext);

    if (NT_SUCCESS(Irp->IoStatus.Status))
        Invoke = (Hook->OriginalControl & SL_INVOKE_ON_SUCCESS) != 0;
    else
        Invoke = (Hook->OriginalControl & SL_INVOKE_ON_ERROR) ||
                 (Irp->Cancel && (Hook->OriginalControl & SL_INVOKE_ON_CANCEL));

    if (Invoke)
    {
        if (Hook->OriginalRoutine)
            Status = Hook->OriginalRoutine(DeviceObject, Irp, Hook->OriginalContext);
    }
    else if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
        Status = STATUS_PENDING;
    }

    ExFreePoolWithTag(Hook, TAG_KSE);
    return Status;
}

static
NTSTATUS
NTAPI
KsepSetCompletionHook(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PIO_COMPLETION_ROUTINE CompletionRoutine,
    _In_opt_ PVOID Context)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PKSE_COMPLETION_HOOK Hook;

    Hook = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Hook), TAG_KSE);
    if (!Hook)
        return STATUS_INSUFFICIENT_RESOURCES;

    Hook->DeviceObject = DeviceObject;
    Hook->HookRoutine = CompletionRoutine;
    Hook->HookContext = Context;
    Hook->OriginalRoutine = Stack->CompletionRoutine;
    Hook->OriginalContext = Stack->Context;
    Hook->OriginalControl = Stack->Control;

    Stack->CompletionRoutine = KsepCompletionSafeWrapper;
    Stack->Context = Hook;
    Stack->Control = SL_INVOKE_ON_SUCCESS | SL_INVOKE_ON_ERROR | SL_INVOKE_ON_CANCEL;
    return STATUS_SUCCESS;
}

static
PLDR_DATA_TABLE_ENTRY
KsepFindModule(
    _In_ PVOID Address)
{
    PLIST_ENTRY Entry;

    for (Entry = PsLoadedModuleList.Flink; Entry != &PsLoadedModuleList; Entry = Entry->Flink)
    {
        PLDR_DATA_TABLE_ENTRY LdrEntry = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);

        if ((ULONG_PTR)Address >= (ULONG_PTR)LdrEntry->DllBase &&
            (ULONG_PTR)Address < (ULONG_PTR)LdrEntry->DllBase + LdrEntry->SizeOfImage)
        {
            return LdrEntry;
        }
    }

    return NULL;
}

static
BOOLEAN
KsepValidateShimProviderAndData(
    _In_ PVOID CallerAddress,
    _In_ PKSE_SHIM Shim)
{
    PLDR_DATA_TABLE_ENTRY Provider;
    PKSE_HOOK_COLLECTION Collection;
    PKSE_HOOK Hook;
    ULONG_PTR Start, End;
    BOOLEAN Valid = FALSE;

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&PsLoadedModuleResource, TRUE);

    Provider = KsepFindModule(CallerAddress);
    if (!Provider || !Shim->HookCollectionsArray)
        goto Done;

    Start = (ULONG_PTR)Provider->DllBase;
    End = Start + Provider->SizeOfImage;
    for (Collection = Shim->HookCollectionsArray; Collection->Type != KSE_HOOK_COLLECTION_END; Collection++)
    {
        if (!Collection->HookArray)
            goto Done;

        for (Hook = Collection->HookArray; Hook->Type != KSE_HOOK_END; Hook++)
        {
            if (!Hook->HookFunction ||
                (ULONG_PTR)Hook->HookFunction < Start ||
                (ULONG_PTR)Hook->HookFunction >= End)
            {
                goto Done;
            }
        }
    }

    Valid = TRUE;

Done:
    ExReleaseResourceLite(&PsLoadedModuleResource);
    KeLeaveCriticalRegion();
    return Valid;
}

static
PKSE_SHIM_REGISTRATION
KsepFindRegistration(
    _In_ const GUID *ShimGuid)
{
    PLIST_ENTRY Entry;

    for (Entry = KseEngine.ShimList.Flink; Entry != &KseEngine.ShimList; Entry = Entry->Flink)
    {
        PKSE_SHIM_REGISTRATION Registration = CONTAINING_RECORD(Entry, KSE_SHIM_REGISTRATION, Link);

        if (!(Registration->Flags & KSE_REGISTRATION_UNREGISTER_PENDING) &&
            RtlEqualMemory(Registration->Shim->ShimGuid, ShimGuid, sizeof(GUID)))
        {
            return Registration;
        }
    }

    return NULL;
}

static
NTSTATUS
KsepRegisterShim(
    _In_ PKSE_SHIM Shim,
    _In_ ULONG Flags,
    _In_opt_ PVOID Context,
    _In_ PVOID CallerAddress)
{
    PKSE_SHIM_REGISTRATION Registration;

    if (!Shim)
        return STATUS_INVALID_PARAMETER;
    if (ReadAcquire(&KseEngine.State) != KSE_ENGINE_INITIALIZED)
        return STATUS_UNSUCCESSFUL;
    if (!Shim->ShimGuid || !KsepValidateShimProviderAndData(CallerAddress, Shim))
        return STATUS_UNSUCCESSFUL;

    Registration = ExAllocatePoolWithTag(PagedPool, sizeof(*Registration), TAG_KSE);
    if (!Registration)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&KseEngine.Lock);
    if (KsepFindRegistration(Shim->ShimGuid))
    {
        ExReleasePushLockExclusive(&KseEngine.Lock);
        KeLeaveCriticalRegion();
        ExFreePoolWithTag(Registration, TAG_KSE);
        return STATUS_OBJECT_NAME_COLLISION;
    }

    Registration->Shim = Shim;
    Registration->RefCount = 0;
    Registration->Flags = Flags;
    Registration->Context = Context;
    InsertTailList(&KseEngine.ShimList, &Registration->Link);
    ExReleasePushLockExclusive(&KseEngine.Lock);
    KeLeaveCriticalRegion();

    Shim->KseCallbackRoutines = &KseEngine.Callbacks;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KseRegisterShimEx(
    _In_ PKSE_SHIM Shim,
    _In_opt_ PVOID Reserved,
    _In_ ULONG Flags,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Reserved);
    return KsepRegisterShim(Shim, Flags, Context, _ReturnAddress());
}

NTSTATUS
NTAPI
KseRegisterShim(
    _In_ PKSE_SHIM Shim,
    _In_opt_ PVOID Reserved,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Reserved);
    return KsepRegisterShim(Shim, Flags, NULL, _ReturnAddress());
}

NTSTATUS
NTAPI
KseUnregisterShim(
    _In_ PKSE_SHIM Shim,
    _In_opt_ PVOID Reserved1,
    _In_opt_ PVOID Reserved2)
{
    PKSE_SHIM_REGISTRATION Registration;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Reserved1);
    UNREFERENCED_PARAMETER(Reserved2);

    if (!Shim)
        return STATUS_INVALID_PARAMETER;
    if (ReadAcquire(&KseEngine.State) != KSE_ENGINE_INITIALIZED)
        return STATUS_UNSUCCESSFUL;

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&KseEngine.Lock);
    Registration = Shim->ShimGuid ? KsepFindRegistration(Shim->ShimGuid) : NULL;
    if (!Registration)
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }
    else if (Registration->RefCount != 0)
    {
        Registration->Flags |= KSE_REGISTRATION_UNREGISTER_PENDING;
        Registration = NULL;
        Status = STATUS_ACCESS_DENIED;
    }
    else
    {
        RemoveEntryList(&Registration->Link);
        Status = STATUS_SUCCESS;
    }
    ExReleasePushLockExclusive(&KseEngine.Lock);
    KeLeaveCriticalRegion();

    if (NT_SUCCESS(Status))
        ExFreePoolWithTag(Registration, TAG_KSE);
    return Status;
}

NTSTATUS
NTAPI
KseQueryDeviceData(
    _In_ PCWSTR DeviceId,
    _In_ PCWSTR Provider,
    _Inout_ PULONG Type,
    _Inout_ PULONG Size,
    _Out_writes_bytes_opt_(*Size) PVOID Data)
{
    ULONG Requested;
    NTSTATUS Status;

    if (!KsepIsEngineReady(KSE_ENGINE_DEVICE_FLAGS_DISABLED))
        return STATUS_NOT_FOUND;
    if (!DeviceId || !Provider || !Type || !Size)
        return STATUS_INVALID_PARAMETER;

    Requested = *Type;
    *Type = 0;
    if (Requested & KSE_DEVICE_DATA_SKIP_REGISTRY)
        return STATUS_NOT_FOUND;

    Status = KsepQueryRegistryDeviceData(DeviceId, Provider, Type, Size, Data);
    if (NT_SUCCESS(Status) && *Size == 0)
        Status = STATUS_NOT_FOUND;
    return Status;
}

NTSTATUS
NTAPI
KseQueryDeviceDataList(
    _In_ PCWSTR DeviceId,
    _Out_writes_bytes_opt_(BufferSize) PWSTR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG RequiredSize)
{
    if (!KsepIsEngineReady(KSE_ENGINE_DEVICE_FLAGS_DISABLED))
        return STATUS_NOT_FOUND;
    if (!DeviceId || !RequiredSize)
        return STATUS_INVALID_PARAMETER;

    return KsepQueryRegistryDeviceDataList(DeviceId, Buffer, BufferSize, RequiredSize);
}

NTSTATUS
NTAPI
KseQueryDeviceFlags(
    _In_ PCWSTR DeviceId,
    _In_ PCWSTR Provider,
    _Out_ PULONG64 Flags)
{
    ULONG Type = REG_QWORD, Size = sizeof(ULONG64);
    ULONG64 Data = 0;
    NTSTATUS Status;

    if (!KsepIsEngineReady(KSE_ENGINE_DEVICE_FLAGS_DISABLED))
        return STATUS_NOT_FOUND;
    if (!DeviceId || !Provider || !Flags)
        return STATUS_INVALID_PARAMETER;

    *Flags = 0;
    Status = KseQueryDeviceData(DeviceId, Provider, &Type, &Size, &Data);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((Type & ~(KSE_DEVICE_DATA_FROM_REGISTRY | KSE_DEVICE_DATA_SKIP_REGISTRY)) != REG_QWORD)
        return STATUS_NOT_FOUND;

    *Flags = Data;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KseSetDeviceFlags(
    _In_ PCWSTR DeviceId,
    _In_ PCWSTR Provider,
    _In_ ULONG64 Flags)
{
    UNICODE_STRING ValueName;
    HANDLE KeyHandle;
    NTSTATUS Status;

    if (!KsepIsEngineReady(KSE_ENGINE_DEVICE_FLAGS_DISABLED))
        return STATUS_UNSUCCESSFUL;
    if (!DeviceId || !Provider)
        return STATUS_INVALID_PARAMETER;

    Status = KsepOpenDeviceKey(DeviceId, KEY_SET_VALUE, TRUE, &KeyHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&ValueName, Provider);
    Status = ZwSetValueKey(KeyHandle, &ValueName, 0, REG_QWORD, &Flags, sizeof(Flags));
    ZwClose(KeyHandle);
    return Status;
}

CODE_SEG("INIT")
VOID
NTAPI
KseInitialize(VOID)
{
    HANDLE KeyHandle;
    ULONG Value, Flags = 0;

    InitializeListHead(&KseEngine.ShimList);
    ExInitializePushLock(&KseEngine.Lock);
    KseEngine.Callbacks.GetIoCallbacks = KsepGetIoCallbacks;
    KseEngine.Callbacks.SetCompletionHook = KsepSetCompletionHook;

    if (NT_SUCCESS(KsepOpenKey(KsepPolicyKey, NULL, KEY_QUERY_VALUE, FALSE, &KeyHandle)))
    {
        if (NT_SUCCESS(KsepQueryDword(KeyHandle, L"DisableDeviceFlags", &Value)) && Value == 1)
            Flags |= KSE_ENGINE_DEVICE_FLAGS_DISABLED;
        if (NT_SUCCESS(KsepQueryDword(KeyHandle, L"DisableDriverShims", &Value)) && Value == 1)
            Flags |= KSE_ENGINE_DRIVER_SHIMS_DISABLED;
        ZwClose(KeyHandle);
    }

    if (NT_SUCCESS(KsepOpenKey(KsepCompatibilityKey, NULL, KEY_QUERY_VALUE, FALSE, &KeyHandle)))
    {
        if (NT_SUCCESS(KsepQueryDword(KeyHandle, L"DisableFlags", &Value)))
            Flags |= Value & (KSE_ENGINE_DRIVER_SHIMS_DISABLED | KSE_ENGINE_DEVICE_FLAGS_DISABLED);
        ZwClose(KeyHandle);
    }

    KseEngine.Flags = Flags;
    if ((Flags & (KSE_ENGINE_DRIVER_SHIMS_DISABLED | KSE_ENGINE_DEVICE_FLAGS_DISABLED)) ==
        (KSE_ENGINE_DRIVER_SHIMS_DISABLED | KSE_ENGINE_DEVICE_FLAGS_DISABLED))
    {
        return;
    }

    WriteRelease(&KseEngine.State, KSE_ENGINE_INITIALIZED);
}
