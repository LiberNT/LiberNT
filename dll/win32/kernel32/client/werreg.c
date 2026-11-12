/*
 * PROJECT:     LiberNT Win32 Base API
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Per-process Windows Error Reporting registration block
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <k32.h>
#include <basewer.h>

#define NDEBUG
#include <debug.h>

PBASE_WER_REGISTRATION
WINAPI
BaseWerGetRegistration(
    _In_ BOOLEAN Create)
{
    PPEB Peb = NtCurrentPeb();
    PBASE_WER_REGISTRATION Registration, Existing;

    Registration = ReadPointerAcquire(&Peb->WerRegistrationData);
    if (Registration || !Create)
        return Registration;

    Registration = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Registration));
    if (!Registration)
        return NULL;

    Registration->Signature = BASE_WER_REGISTRATION_SIGNATURE;
    RtlInitializeSRWLock(&Registration->Lock);

    Existing = InterlockedCompareExchangePointer(&Peb->WerRegistrationData, Registration, NULL);
    if (Existing)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Registration);
        return Existing;
    }

    return Registration;
}

LONG
WINAPI
BaseWerGetState(VOID)
{
    PBASE_WER_REGISTRATION Registration = BaseWerGetRegistration(FALSE);

    return Registration ? ReadAcquire(&Registration->State) : 0;
}

static
HRESULT
BasepWerStatusToHResult(
    _In_ NTSTATUS Status)
{
    return HRESULT_FROM_WIN32(RtlNtStatusToDosError(Status));
}

static
NTSTATUS
BasepWerReadRemoteAddress(
    _In_ HANDLE Process,
    _In_ PPROCESS_BASIC_INFORMATION BasicInformation,
    _Out_ PULONG_PTR Address)
{
    ULONG_PTR Peb32 = 0;
    ULONG Address32 = 0;
    NTSTATUS Status;

    *Address = 0;

    if (sizeof(PVOID) != sizeof(ULONG))
    {
        Status = NtQueryInformationProcess(Process, ProcessWow64Information, &Peb32, sizeof(Peb32), NULL);
        if (!NT_SUCCESS(Status))
            return Status;

        if (Peb32)
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

    if (!BasicInformation->PebBaseAddress)
    {
        /* TODO: read the registration of a native 64-bit process from a WoW64 caller */
        return STATUS_NOT_SUPPORTED;
    }

    return NtReadVirtualMemory(Process,
                               (PUCHAR)BasicInformation->PebBaseAddress + FIELD_OFFSET(PEB, WerRegistrationData),
                               Address,
                               sizeof(*Address),
                               NULL);
}

static
NTSTATUS
BasepWerQueryProcess(
    _In_ HANDLE Process,
    _Out_ PPROCESS_BASIC_INFORMATION BasicInformation,
    _Out_ PBOOLEAN Current)
{
    NTSTATUS Status;

    RtlZeroMemory(BasicInformation, sizeof(*BasicInformation));
    *Current = TRUE;
    if (Process == NtCurrentProcess())
        return STATUS_SUCCESS;

    Status = NtQueryInformationProcess(Process,
                                       ProcessBasicInformation,
                                       BasicInformation,
                                       sizeof(*BasicInformation),
                                       NULL);
    if (NT_SUCCESS(Status))
        *Current = BasicInformation->UniqueProcessId == (ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;

    return Status;
}

HRESULT
WINAPI
BaseWerQueryRegistration(
    _In_ HANDLE Process,
    _Out_ PBASE_WER_REGISTRATION Registration)
{
    PROCESS_BASIC_INFORMATION BasicInformation;
    PBASE_WER_REGISTRATION Local;
    ULONG_PTR Address;
    BOOLEAN Current;
    NTSTATUS Status;

    RtlZeroMemory(Registration, sizeof(*Registration));

    Status = BasepWerQueryProcess(Process, &BasicInformation, &Current);
    if (!NT_SUCCESS(Status))
        return BasepWerStatusToHResult(Status);

    if (!Current)
    {
        Status = BasepWerReadRemoteAddress(Process, &BasicInformation, &Address);
        if (NT_SUCCESS(Status) && Address)
            Status = NtReadVirtualMemory(Process, (PVOID)Address, Registration, BASE_WER_SHARED_SIZE, NULL);

        if (!NT_SUCCESS(Status) || Registration->Signature != BASE_WER_REGISTRATION_SIGNATURE)
            RtlZeroMemory(Registration, sizeof(*Registration));

        return NT_SUCCESS(Status) ? S_OK : BasepWerStatusToHResult(Status);
    }

    Local = BaseWerGetRegistration(FALSE);
    if (Local)
    {
        RtlAcquireSRWLockShared(&Local->Lock);
        RtlCopyMemory(Registration, Local, BASE_WER_SHARED_SIZE);
        RtlReleaseSRWLockShared(&Local->Lock);
    }

    return S_OK;
}

HRESULT
WINAPI
BaseWerReadRegistrationMemory(
    _In_ HANDLE Process,
    _In_ ULONG64 Address,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    PROCESS_BASIC_INFORMATION BasicInformation;
    PBASE_WER_REGISTRATION Local;
    BOOLEAN Current;
    NTSTATUS Status;

    Status = BasepWerQueryProcess(Process, &BasicInformation, &Current);
    if (!NT_SUCCESS(Status))
        return BasepWerStatusToHResult(Status);

    if (!Current)
    {
        Status = NtReadVirtualMemory(Process, (PVOID)(ULONG_PTR)Address, Buffer, Size, NULL);
        return NT_SUCCESS(Status) ? S_OK : BasepWerStatusToHResult(Status);
    }

    Local = BaseWerGetRegistration(FALSE);
    if (!Local)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    RtlAcquireSRWLockShared(&Local->Lock);
    RtlCopyMemory(Buffer, (PVOID)(ULONG_PTR)Address, Size);
    RtlReleaseSRWLockShared(&Local->Lock);
    return S_OK;
}
