/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Performance Counter for Windows provider compatibility
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <ntintsafe.h>
#define NDEBUG
#include <debug.h>

/* TYPES *********************************************************************/

#define PCW_REGISTRATION_TAG 'wcPP'
#define PCW_INSTANCE_TAG     'icPP'

typedef struct _PCW_REGISTRATION
{
    KGUARDED_MUTEX Lock;
    LIST_ENTRY Instances;
    UNICODE_STRING Name;
    ULONG CounterCount;
    PPCW_COUNTER_DESCRIPTOR Counters;
    PPCW_CALLBACK Callback;
    PVOID CallbackContext;
    ULONG Flags;
    ULONG NextInstanceId;
} PCW_REGISTRATION;

typedef struct _PCW_INSTANCE
{
    LIST_ENTRY Link;
    PPCW_REGISTRATION Registration;
    UNICODE_STRING Name;
    ULONG Id;
    ULONG Count;
    PCW_DATA Data[ANYSIZE_ARRAY];
} PCW_INSTANCE;

/* FUNCTIONS *****************************************************************/

/* Whether the data blocks hold every counter the registration describes */
static
NTSTATUS
PcwpValidateData(
    _In_ PPCW_REGISTRATION Registration,
    _In_ ULONG Count,
    _In_reads_(Count) PPCW_DATA Data)
{
    PPCW_COUNTER_DESCRIPTOR Counter;
    ULONG i;

    for (i = 0; i < Registration->CounterCount; i++)
    {
        Counter = &Registration->Counters[i];

        if (Counter->StructIndex >= Count)
            return STATUS_INVALID_PARAMETER_4;

        if ((ULONG)Counter->Offset + Counter->Size > Data[Counter->StructIndex].Size)
            return STATUS_INVALID_BUFFER_SIZE;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PcwRegister(
    _Outptr_ PPCW_REGISTRATION *Registration,
    _In_ PPCW_REGISTRATION_INFORMATION Info)
{
    PPCW_REGISTRATION NewRegistration;
    PCUNICODE_STRING Name;
    SIZE_T CountersSize, Size;
    ULONG Flags = PcwRegistrationNone;

    PAGED_CODE();

    if (Registration == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Info == NULL)
        return STATUS_INVALID_PARAMETER_2;

    if (Info->Version != PCW_VERSION_1 && Info->Version != PCW_VERSION_2)
        return STATUS_INVALID_PARAMETER_2;

    /* A first version caller's structure ends before the flags */
    if (Info->Version >= PCW_VERSION_2)
    {
        if (Info->Flags & ~PcwRegistrationSiloNeutral)
            return STATUS_INVALID_PARAMETER_2;
        Flags = Info->Flags;
    }

    Name = Info->Name;
    if (Name == NULL || Name->Length == 0 || (Name->Length % sizeof(WCHAR)) != 0 || Name->Buffer == NULL)
        return STATUS_INVALID_PARAMETER_2;

    if (Info->CounterCount != 0 && Info->Counters == NULL)
        return STATUS_INVALID_PARAMETER_2;

    if (!NT_SUCCESS(RtlSIZETMult(Info->CounterCount, sizeof(PCW_COUNTER_DESCRIPTOR), &CountersSize)) ||
        !NT_SUCCESS(RtlSIZETAdd(sizeof(PCW_REGISTRATION), CountersSize, &Size)) ||
        !NT_SUCCESS(RtlSIZETAdd(Size, Name->Length, &Size)))
    {
        return STATUS_INTEGER_OVERFLOW;
    }

    NewRegistration = ExAllocatePoolZero(NonPagedPoolNx, Size, PCW_REGISTRATION_TAG);
    if (NewRegistration == NULL)
        return STATUS_NO_MEMORY;

    KeInitializeGuardedMutex(&NewRegistration->Lock);
    InitializeListHead(&NewRegistration->Instances);

    /* Everything the caller described is kept, so it need not keep it itself */
    NewRegistration->Counters = (PPCW_COUNTER_DESCRIPTOR)(NewRegistration + 1);
    NewRegistration->CounterCount = Info->CounterCount;
    if (CountersSize != 0)
        RtlCopyMemory(NewRegistration->Counters, Info->Counters, CountersSize);

    NewRegistration->Name.Buffer = (PWCHAR)((PUCHAR)NewRegistration->Counters + CountersSize);
    NewRegistration->Name.Length = Name->Length;
    NewRegistration->Name.MaximumLength = Name->Length;
    RtlCopyMemory(NewRegistration->Name.Buffer, Name->Buffer, Name->Length);

    NewRegistration->Callback = Info->Callback;
    NewRegistration->CallbackContext = Info->CallbackContext;
    NewRegistration->Flags = Flags;

    /* TODO: tie a registration that is not silo-neutral to the server silo attached to the thread */
    *Registration = NewRegistration;
    return STATUS_SUCCESS;
}

VOID
NTAPI
PcwUnregister(
    _In_ PPCW_REGISTRATION Registration)
{
    PPCW_INSTANCE Instance;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (Registration == NULL)
        return;

    /* The instances still open are closed with their registration */
    KeAcquireGuardedMutex(&Registration->Lock);
    while (!IsListEmpty(&Registration->Instances))
    {
        Entry = RemoveHeadList(&Registration->Instances);
        Instance = CONTAINING_RECORD(Entry, PCW_INSTANCE, Link);
        ExFreePoolWithTag(Instance, PCW_INSTANCE_TAG);
    }
    KeReleaseGuardedMutex(&Registration->Lock);

    ExFreePoolWithTag(Registration, PCW_REGISTRATION_TAG);
}

NTSTATUS
NTAPI
PcwAddInstance(
    _In_ PPCW_BUFFER Buffer,
    _In_ PCUNICODE_STRING Name,
    _In_ ULONG Id,
    _In_ ULONG Count,
    _In_reads_(Count) PPCW_DATA Data)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Id);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Data);

    /*
     * No PCW consumer exists yet, so there is no system buffer to populate.
     * Returning success keeps optional provider registration non-fatal.
     */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PcwCreateInstance(
    _Outptr_ PPCW_INSTANCE *Instance,
    _In_ PPCW_REGISTRATION Registration,
    _In_ PCUNICODE_STRING Name,
    _In_ ULONG Count,
    _In_reads_(Count) PPCW_DATA Data)
{
    PPCW_INSTANCE NewInstance;
    SIZE_T DataSize, Size;
    NTSTATUS Status;

    PAGED_CODE();

    if ((Instance == NULL) || (Registration == NULL) || (Name == NULL) ||
        ((Name->Length != 0) && (Name->Buffer == NULL)) ||
        ((Count != 0) && (Data == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = PcwpValidateData(Registration, Count, Data);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!NT_SUCCESS(RtlSIZETMult(Count, sizeof(PCW_DATA), &DataSize)) ||
        !NT_SUCCESS(RtlSIZETAdd(FIELD_OFFSET(PCW_INSTANCE, Data), DataSize, &Size)) ||
        !NT_SUCCESS(RtlSIZETAdd(Size, Name->Length, &Size)))
    {
        return STATUS_INTEGER_OVERFLOW;
    }

    NewInstance = ExAllocatePoolZero(PagedPool, Size, PCW_INSTANCE_TAG);
    if (NewInstance == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    NewInstance->Registration = Registration;
    NewInstance->Count = Count;
    if (DataSize != 0)
        RtlCopyMemory(NewInstance->Data, Data, DataSize);

    NewInstance->Name.Buffer = (PWCHAR)((PUCHAR)NewInstance->Data + DataSize);
    NewInstance->Name.Length = Name->Length;
    NewInstance->Name.MaximumLength = Name->Length;
    if (Name->Length != 0)
        RtlCopyMemory(NewInstance->Name.Buffer, Name->Buffer, Name->Length);

    KeAcquireGuardedMutex(&Registration->Lock);
    NewInstance->Id = Registration->NextInstanceId++;
    InsertTailList(&Registration->Instances, &NewInstance->Link);
    KeReleaseGuardedMutex(&Registration->Lock);

    *Instance = NewInstance;
    return STATUS_SUCCESS;
}

VOID
NTAPI
PcwCloseInstance(
    _In_ PPCW_INSTANCE Instance)
{
    PPCW_REGISTRATION Registration;

    PAGED_CODE();

    if (Instance == NULL)
        return;

    Registration = Instance->Registration;

    KeAcquireGuardedMutex(&Registration->Lock);
    RemoveEntryList(&Instance->Link);
    KeReleaseGuardedMutex(&Registration->Lock);

    ExFreePoolWithTag(Instance, PCW_INSTANCE_TAG);
}
