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
    LIST_ENTRY Entry;
    PVOID Address;
    DWORD Size;
    WER_REGISTER_FILE_TYPE FileType;
    DWORD Flags;
    UNICODE_STRING FileName;
} WER_REGISTERED_ENTRY, *PWER_REGISTERED_ENTRY;

/* TODO: no crash report or dump consumes the registered files and memory blocks (Windows adds them only when the server asks) */
static SRWLOCK WerRegistrationLock = SRWLOCK_INIT;
static LIST_ENTRY WerRegisteredFiles = {&WerRegisteredFiles, &WerRegisteredFiles};
static LIST_ENTRY WerRegisteredMemoryBlocks = {&WerRegisteredMemoryBlocks, &WerRegisteredMemoryBlocks};
static ULONG WerRegisteredEntryCount;

static
BOOL
WerIsRecoveryInProgress(VOID)
{
    return (BaseWerGetState() & BASE_WER_RECOVERY_IN_PROGRESS) != 0;
}

static
HRESULT
WerInsertEntry(
    _In_ PLIST_ENTRY List,
    _In_ PWER_REGISTERED_ENTRY Entry)
{
    AcquireSRWLockExclusive(&WerRegistrationLock);
    if (WerRegisteredEntryCount >= WER_MAX_REGISTERED_ENTRIES)
    {
        ReleaseSRWLockExclusive(&WerRegistrationLock);
        RtlFreeHeap(GetProcessHeap(), 0, Entry);
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    InsertTailList(List, &Entry->Entry);
    WerRegisteredEntryCount++;
    ReleaseSRWLockExclusive(&WerRegistrationLock);
    return S_OK;
}

static
HRESULT
WerRemoveEntry(
    _In_ PLIST_ENTRY List,
    _In_opt_ PVOID Address,
    _In_opt_ PCUNICODE_STRING FileName)
{
    PWER_REGISTERED_ENTRY Entry;
    PLIST_ENTRY Link;

    AcquireSRWLockExclusive(&WerRegistrationLock);
    for (Link = List->Flink; Link != List; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, WER_REGISTERED_ENTRY, Entry);
        if (FileName ? RtlEqualUnicodeString(&Entry->FileName, FileName, TRUE) : Entry->Address == Address)
        {
            RemoveEntryList(&Entry->Entry);
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

    Entry = RtlAllocateHeap(GetProcessHeap(), 0, sizeof(*Entry) + (Length + 1) * sizeof(WCHAR));
    if (!Entry)
        return E_OUTOFMEMORY;

    Entry->Address = NULL;
    Entry->Size = 0;
    Entry->FileType = regFileType;
    Entry->Flags = dwFlags;
    Entry->FileName.Buffer = (PWSTR)(Entry + 1);
    Entry->FileName.Length = (USHORT)(Length * sizeof(WCHAR));
    Entry->FileName.MaximumLength = (USHORT)((Length + 1) * sizeof(WCHAR));
    RtlCopyMemory(Entry->FileName.Buffer, pwzFile, (Length + 1) * sizeof(WCHAR));

    return WerInsertEntry(&WerRegisteredFiles, Entry);
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
    return WerRemoveEntry(&WerRegisteredFiles, NULL, &FileName);
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

    Entry->Address = pvAddress;
    Entry->Size = dwSize;
    return WerInsertEntry(&WerRegisteredMemoryBlocks, Entry);
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

    return WerRemoveEntry(&WerRegisteredMemoryBlocks, pvAddress, NULL);
}
