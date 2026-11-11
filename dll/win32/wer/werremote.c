/*
 * PROJECT:     LiberNT Windows Error Reporting
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading the WER registrations of another process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#define NTOS_MODE_USER
#include <ndk/psfuncs.h>
#include <ndk/mmfuncs.h>
#include <ndk/rtlfuncs.h>
#include <basewer.h>
#include "werremote.h"

static
HRESULT
WerpStatusToHResult(
    _In_ NTSTATUS Status)
{
    return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));
}

static
NTSTATUS
WerpReadBlockAddress(
    _In_ HANDLE Process,
    _Out_ PULONG64 Address)
{
    PROCESS_BASIC_INFORMATION BasicInformation;
    ULONG_PTR Peb32 = 0, Native = 0;
    ULONG Address32 = 0;
    NTSTATUS Status;

    *Address = 0;

    if (sizeof(PVOID) != sizeof(ULONG))
    {
        Status = NtQueryInformationProcess(Process, ProcessWow64Information, &Peb32, sizeof(Peb32), NULL);
        if (NT_SUCCESS(Status) && Peb32)
        {
            Status = NtReadVirtualMemory(Process,
                                         (PVOID)(Peb32 + FIELD_OFFSET(PEB32, WerRegistrationData)),
                                         &Address32,
                                         sizeof(Address32),
                                         NULL);
            *Address = Address32;
            return Status;
        }
    }

    Status = NtQueryInformationProcess(Process,
                                       ProcessBasicInformation,
                                       &BasicInformation,
                                       sizeof(BasicInformation),
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!BasicInformation.PebBaseAddress)
    {
        /* TODO: read the registration of a native 64-bit process from a WoW64 reader */
        return STATUS_NOT_SUPPORTED;
    }

    Status = NtReadVirtualMemory(Process,
                                 (PUCHAR)BasicInformation.PebBaseAddress + FIELD_OFFSET(PEB, WerRegistrationData),
                                 &Native,
                                 sizeof(Native),
                                 NULL);
    *Address = Native;
    return Status;
}

HRESULT
WerRemoteReadRegistration(
    _In_ HANDLE Process,
    _Out_ PULONG64 Address,
    _Out_ PBASE_WER_REGISTRATION Registration)
{
    NTSTATUS Status;

    RtlZeroMemory(Registration, sizeof(*Registration));
    Status = WerpReadBlockAddress(Process, Address);
    if (!NT_SUCCESS(Status))
        return WerpStatusToHResult(Status);

    if (!*Address || *Address != (ULONG_PTR)*Address)
    {
        *Address = 0;
        return S_FALSE;
    }

    Status = NtReadVirtualMemory(Process, (PVOID)(ULONG_PTR)*Address, Registration, BASE_WER_SHARED_SIZE, NULL);
    if (!NT_SUCCESS(Status) || Registration->Signature != BASE_WER_REGISTRATION_SIGNATURE)
    {
        RtlZeroMemory(Registration, sizeof(*Registration));
        *Address = 0;
        return NT_SUCCESS(Status) ? S_FALSE : WerpStatusToHResult(Status);
    }

    return S_OK;
}

HRESULT
WerRemoteReadEntries(
    _In_ HANDLE Process,
    _In_ ULONG64 Head,
    _In_ ULONG MaxEntries,
    _Out_ PWER_REMOTE_ENTRY *Entries,
    _Out_ PULONG Count)
{
    PWER_REMOTE_ENTRY Result;
    BASE_WER_ENTRY Shared;
    ULONG Index, Length;
    NTSTATUS Status;

    *Entries = NULL;
    *Count = 0;
    if (!Head || !MaxEntries)
        return S_OK;

    Result = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, MaxEntries * sizeof(*Result));
    if (!Result)
        return E_OUTOFMEMORY;

    for (Index = 0; Head && Index < MaxEntries; Head = Shared.Next)
    {
        if (Head != (ULONG_PTR)Head)
            break;

        Status = NtReadVirtualMemory(Process, (PVOID)(ULONG_PTR)Head, &Shared, sizeof(Shared), NULL);
        if (!NT_SUCCESS(Status))
            break;

        Result[Index].Address = Shared.Address;
        Result[Index].Size = Shared.Size;
        Result[Index].Type = Shared.Type;
        Result[Index].Flags = Shared.Flags;
        Length = min(Shared.NameLength, (MAX_PATH - 1) * sizeof(WCHAR)) & ~(sizeof(WCHAR) - 1);
        if (Length && Shared.Name == (ULONG_PTR)Shared.Name &&
            !NT_SUCCESS(NtReadVirtualMemory(Process, (PVOID)(ULONG_PTR)Shared.Name, Result[Index].Name, Length, NULL)))
        {
            Length = 0;
        }
        Result[Index].Name[Length / sizeof(WCHAR)] = UNICODE_NULL;
        Index++;
    }

    if (!Index)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Result);
        return S_OK;
    }

    *Entries = Result;
    *Count = Index;
    return S_OK;
}
