/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Error Reporting flags, file and memory block registrations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winternl.h>
#include <werapi.h>
#include <basewer.h>

typedef struct _WER_REGISTERED_ENTRY
{
    BASE_WER_ENTRY Shared;
    WCHAR Name[ANYSIZE_ARRAY];
} WER_REGISTERED_ENTRY, *PWER_REGISTERED_ENTRY;

static SRWLOCK WerRegistrationLock = SRWLOCK_INIT;
static ULONG WerRegisteredEntryCount;

static
BOOL
WerIsRecoveryInProgress(VOID)
{
    return (BaseWerGetState() & BASE_WER_RECOVERY_IN_PROGRESS) != 0;
}

static
PULONG64
WerGetChain(
    _In_ PBASE_WER_REGISTRATION Registration,
    _In_ BOOL Files)
{
    return Files ? &Registration->Files : &Registration->MemoryBlocks;
}

static
HRESULT
WerInsertEntry(
    _In_ BOOL Files,
    _In_ PWER_REGISTERED_ENTRY Entry)
{
    PBASE_WER_REGISTRATION Registration = BaseWerGetRegistration(TRUE);
    PULONG64 Link;

    if (!Registration)
    {
        RtlFreeHeap(GetProcessHeap(), 0, Entry);
        return E_OUTOFMEMORY;
    }

    Entry->Shared.Next = 0;
    AcquireSRWLockExclusive(&WerRegistrationLock);
    if (WerRegisteredEntryCount >= WER_MAX_REGISTERED_ENTRIES)
    {
        ReleaseSRWLockExclusive(&WerRegistrationLock);
        RtlFreeHeap(GetProcessHeap(), 0, Entry);
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    for (Link = WerGetChain(Registration, Files); *Link; Link = &((PBASE_WER_ENTRY)(ULONG_PTR)*Link)->Next);
    InterlockedExchange64((PLONG64)Link, (LONG64)(ULONG_PTR)Entry);
    WerRegisteredEntryCount++;
    ReleaseSRWLockExclusive(&WerRegistrationLock);
    return S_OK;
}

static
HRESULT
WerRemoveEntry(
    _In_ BOOL Files,
    _In_opt_ PVOID Address,
    _In_opt_ PCUNICODE_STRING FileName)
{
    PBASE_WER_REGISTRATION Registration = BaseWerGetRegistration(FALSE);
    PWER_REGISTERED_ENTRY Entry;
    UNICODE_STRING EntryName;
    PULONG64 Link;

    if (!Registration)
        return WER_E_NOT_FOUND;

    AcquireSRWLockExclusive(&WerRegistrationLock);
    for (Link = WerGetChain(Registration, Files); *Link; Link = &Entry->Shared.Next)
    {
        Entry = (PWER_REGISTERED_ENTRY)(ULONG_PTR)*Link;
        EntryName.Buffer = Entry->Name;
        EntryName.Length = EntryName.MaximumLength = (USHORT)Entry->Shared.NameLength;
        if (FileName ? RtlEqualUnicodeString(&EntryName, FileName, TRUE) :
                       Entry->Shared.Address == (ULONG_PTR)Address)
        {
            InterlockedExchange64((PLONG64)Link, (LONG64)Entry->Shared.Next);
            WerRegisteredEntryCount--;
            ReleaseSRWLockExclusive(&WerRegistrationLock);
            RtlFreeHeap(GetProcessHeap(), 0, Entry);
            return S_OK;
        }
    }

    ReleaseSRWLockExclusive(&WerRegistrationLock);
    return WER_E_NOT_FOUND;
}

HRESULT
WINAPI
WerGetFlags(
    _In_ HANDLE hProcess,
    _Out_ PDWORD pdwFlags)
{
    BASE_WER_REGISTRATION Registration;
    HRESULT Result;

    if (!hProcess || !pdwFlags)
        return E_INVALIDARG;

    /* FIXME: a handle with PROCESS_QUERY_INFORMATION but without PROCESS_VM_READ cannot read another process' flags */
    Result = BaseWerQueryRegistration(hProcess, &Registration);
    if (FAILED(Result))
        return Result;

    if (!(Registration.State & BASE_WER_FLAGS_SET))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    *pdwFlags = Registration.Flags;
    return S_OK;
}

HRESULT
WINAPI
WerSetFlags(
    _In_ DWORD dwFlags)
{
    PBASE_WER_REGISTRATION Registration = BaseWerGetRegistration(TRUE);

    if (!Registration)
        return E_OUTOFMEMORY;

    AcquireSRWLockExclusive(&Registration->Lock);
    Registration->Flags = dwFlags;
    InterlockedOr(&Registration->State, BASE_WER_FLAGS_SET);
    ReleaseSRWLockExclusive(&Registration->Lock);
    return S_OK;
}

HRESULT
WINAPI
WerRegisterFile(
    _In_ PCWSTR pwzFile,
    _In_ WER_REGISTER_FILE_TYPE regFileType,
    _In_ DWORD dwFlags)
{
    PWER_REGISTERED_ENTRY Entry;
    SIZE_T Length;

    if (!pwzFile || !pwzFile[0] ||
        (regFileType != WerRegFileTypeUserDocument && regFileType != WerRegFileTypeOther))
    {
        return E_INVALIDARG;
    }

    Length = wcsnlen(pwzFile, MAX_PATH);
    if (Length >= MAX_PATH)
        return E_INVALIDARG;

    if (WerIsRecoveryInProgress())
        return WER_E_INVALID_STATE;

    Entry = RtlAllocateHeap(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            FIELD_OFFSET(WER_REGISTERED_ENTRY, Name[Length + 1]));
    if (!Entry)
        return E_OUTOFMEMORY;

    Entry->Shared.Type = regFileType;
    Entry->Shared.Flags = dwFlags;
    Entry->Shared.NameLength = (ULONG)(Length * sizeof(WCHAR));
    Entry->Shared.Name = (ULONG_PTR)Entry->Name;
    RtlCopyMemory(Entry->Name, pwzFile, (Length + 1) * sizeof(WCHAR));

    return WerInsertEntry(TRUE, Entry);
}

HRESULT
WINAPI
WerUnregisterFile(
    _In_ PCWSTR pwzFilePath)
{
    UNICODE_STRING FileName;

    if (!pwzFilePath)
        return E_INVALIDARG;

    if (WerIsRecoveryInProgress())
        return WER_E_INVALID_STATE;

    RtlInitUnicodeString(&FileName, pwzFilePath);
    return WerRemoveEntry(TRUE, NULL, &FileName);
}

HRESULT
WINAPI
WerRegisterMemoryBlock(
    _In_ PVOID pvAddress,
    _In_ DWORD dwSize)
{
    PWER_REGISTERED_ENTRY Entry;

    if (!pvAddress || !dwSize || dwSize > WER_MAX_MEM_BLOCK_SIZE)
        return E_INVALIDARG;

    if (WerIsRecoveryInProgress())
        return WER_E_INVALID_STATE;

    Entry = RtlAllocateHeap(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Entry));
    if (!Entry)
        return E_OUTOFMEMORY;

    Entry->Shared.Address = (ULONG_PTR)pvAddress;
    Entry->Shared.Size = dwSize;
    return WerInsertEntry(FALSE, Entry);
}

HRESULT
WINAPI
WerUnregisterMemoryBlock(
    _In_ PVOID pvAddress)
{
    if (!pvAddress)
        return E_INVALIDARG;

    if (WerIsRecoveryInProgress())
        return WER_E_INVALID_STATE;

    return WerRemoveEntry(FALSE, pvAddress, NULL);
}
