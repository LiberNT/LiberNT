/*
 * PROJECT:     LiberNT NT User-Mode DLL
 * PURPOSE:     Windows Notification Facility user-mode entry points
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

NTSTATUS
NTAPI
RtlQueryWnfStateData(
    _Out_ PULONG ChangeStamp,
    _In_ ULONGLONG StateName,
    _In_ PVOID Callback,
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID TypeId,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(StateName);
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(CallbackContext);
    UNREFERENCED_PARAMETER(TypeId);
    UNREFERENCED_PARAMETER(Flags);

    DPRINT("RtlQueryWnfStateData: no WNF provider\n");

    if (ChangeStamp)
        *ChangeStamp = 0;

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
RtlSubscribeWnfStateChangeNotification(
    _Outptr_ PVOID *Subscription,
    _In_ ULONGLONG StateName,
    _In_ ULONG ChangeStamp,
    _In_ PVOID Callback,
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID TypeId,
    _In_ ULONG SerializationGroup,
    _In_ ULONG Unknown,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(StateName);
    UNREFERENCED_PARAMETER(ChangeStamp);
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(CallbackContext);
    UNREFERENCED_PARAMETER(TypeId);
    UNREFERENCED_PARAMETER(SerializationGroup);
    UNREFERENCED_PARAMETER(Unknown);
    UNREFERENCED_PARAMETER(Flags);

    DPRINT("RtlSubscribeWnfStateChangeNotification: no WNF provider\n");

    if (Subscription)
        *Subscription = NULL;

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
RtlUnsubscribeWnfNotificationWaitForCompletion(
    _In_opt_ PVOID Subscription)
{
    UNREFERENCED_PARAMETER(Subscription);

    DPRINT("RtlUnsubscribeWnfNotificationWaitForCompletion: no WNF provider\n");
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
RtlUnsubscribeWnfStateChangeNotification(
    _In_ PVOID Subscription)
{
    UNREFERENCED_PARAMETER(Subscription);

    DPRINT("RtlUnsubscribeWnfStateChangeNotification: no WNF provider\n");
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
RtlCheckPortableOperatingSystem(
    _Out_ PBOOLEAN IsPortable)
{
    UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"PortableOperatingSystem");
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Value = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle;
    ULONG ResultLength;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             Buffer,
                             sizeof(Buffer),
                             &ResultLength);
    NtClose(KeyHandle);

    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        return STATUS_NOT_FOUND;
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
        return Status;
    if (Value->Type != REG_DWORD || Value->DataLength != sizeof(ULONG))
        return STATUS_OBJECT_TYPE_MISMATCH;

    *IsPortable = (*(PULONG)Value->Data != 0);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlFlushHeaps(VOID)
{
    HANDLE Heaps[64];
    HANDLE *HeapArray = Heaps;
    ULONG Count, Index;
    SIZE_T Capacity;
    NTSTATUS Status = STATUS_SUCCESS;

    Count = RtlGetProcessHeaps(RTL_NUMBER_OF(Heaps), Heaps);
    Capacity = RTL_NUMBER_OF(Heaps);
    while (Count > Capacity)
    {
        if (HeapArray != Heaps)
            RtlFreeHeap(RtlGetProcessHeap(), 0, HeapArray);
        Capacity = Count;
        if (Capacity > MAXULONG_PTR / sizeof(HANDLE))
            return STATUS_NO_MEMORY;
        HeapArray = RtlAllocateHeap(RtlGetProcessHeap(), 0, (SIZE_T)Capacity * sizeof(HANDLE));
        if (!HeapArray)
            return STATUS_NO_MEMORY;

        Count = RtlGetProcessHeaps(Capacity, HeapArray);
    }

    for (Index = 0; Index < Count; Index++)
        RtlCompactHeap(HeapArray[Index], 0);

    if (HeapArray != Heaps)
        RtlFreeHeap(RtlGetProcessHeap(), 0, HeapArray);

    return Status;
}

VOID
NTAPI
WinSqmIncrementDWORD(
    _In_ ULONG Unknown1,
    _In_ ULONG Unknown2,
    _In_ ULONG Unknown3)
{
    UNREFERENCED_PARAMETER(Unknown1);
    UNREFERENCED_PARAMETER(Unknown2);
    UNREFERENCED_PARAMETER(Unknown3);
}

VOID
NTAPI
WinSqmSetDWORD(
    _In_opt_ HANDLE Session,
    _In_ ULONG DatapointId,
    _In_ ULONG Value)
{
    UNREFERENCED_PARAMETER(Session);
    UNREFERENCED_PARAMETER(DatapointId);
    UNREFERENCED_PARAMETER(Value);
}

VOID
NTAPI
WinSqmSetDWORD64(
    _In_opt_ HANDLE Session,
    _In_ ULONG DatapointId,
    _In_ ULONGLONG Value)
{
    UNREFERENCED_PARAMETER(Session);
    UNREFERENCED_PARAMETER(DatapointId);
    UNREFERENCED_PARAMETER(Value);
}

BOOL
NTAPI
WinSqmIsOptedIn(VOID)
{
    return FALSE;
}

BOOL
NTAPI
WinSqmEventEnabled(
    _In_ ULONG EventId,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(EventId);
    UNREFERENCED_PARAMETER(Context);

    return FALSE;
}
