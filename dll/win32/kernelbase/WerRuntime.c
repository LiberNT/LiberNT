/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Error Reporting runtime exception module registrations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <winternl.h>
#include <werapi.h>
#include <basewer.h>

typedef struct _WER_RUNTIME_MODULE
{
    BASE_WER_ENTRY Shared;
    WCHAR Name[ANYSIZE_ARRAY];
} WER_RUNTIME_MODULE, *PWER_RUNTIME_MODULE;

static SRWLOCK WerRuntimeLock = SRWLOCK_INIT;
static ULONG WerRuntimeModuleCount;

static PULONG64
FindRuntimeModule(
    _In_ PBASE_WER_REGISTRATION Registration,
    _In_ PCUNICODE_STRING DllName,
    _In_opt_ PVOID Context)
{
    PWER_RUNTIME_MODULE Module;
    UNICODE_STRING Name;
    PULONG64 Link;

    for (Link = &Registration->RuntimeModules; *Link; Link = &Module->Shared.Next)
    {
        Module = (PWER_RUNTIME_MODULE)(ULONG_PTR)*Link;
        Name.Buffer = Module->Name;
        Name.Length = Name.MaximumLength = (USHORT)Module->Shared.NameLength;
        if (Module->Shared.Address == (ULONG_PTR)Context && RtlEqualUnicodeString(&Name, DllName, TRUE)) return Link;
    }

    return NULL;
}

HRESULT
WINAPI
WerRegisterRuntimeExceptionModule(
    _In_ PCWSTR DllName,
    _In_opt_ PVOID Context)
{
    PBASE_WER_REGISTRATION Registration;
    PWER_RUNTIME_MODULE Module;
    UNICODE_STRING Name;
    PULONG64 Link;

    if (!DllName) return E_INVALIDARG;
    if (BaseWerGetState() & BASE_WER_RECOVERY_IN_PROGRESS) return WER_E_INVALID_STATE;
    RtlInitUnicodeString(&Name, DllName);
    Registration = BaseWerGetRegistration(TRUE);
    if (!Registration) return E_OUTOFMEMORY;
    Module = RtlAllocateHeap(NtCurrentTeb()->Peb->ProcessHeap, HEAP_ZERO_MEMORY,
                             FIELD_OFFSET(WER_RUNTIME_MODULE, Name[Name.Length / sizeof(WCHAR) + 1]));
    if (!Module) return E_OUTOFMEMORY;

    RtlCopyMemory(Module->Name, Name.Buffer, Name.Length);
    Module->Shared.NameLength = Name.Length;
    Module->Shared.Name = (ULONG_PTR)Module->Name;
    Module->Shared.Address = (ULONG_PTR)Context;

    RtlAcquireSRWLockExclusive(&WerRuntimeLock);
    if (FindRuntimeModule(Registration, &Name, Context))
    {
        RtlReleaseSRWLockExclusive(&WerRuntimeLock);
        RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Module);
        return S_OK;
    }
    if (WerRuntimeModuleCount == WER_MAX_REGISTERED_RUNTIME_EXCEPTION_MODULES)
    {
        RtlReleaseSRWLockExclusive(&WerRuntimeLock);
        RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Module);
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    for (Link = &Registration->RuntimeModules; *Link; Link = &((PBASE_WER_ENTRY)(ULONG_PTR)*Link)->Next);
    InterlockedExchange64((PLONG64)Link, (LONG64)(ULONG_PTR)Module);
    WerRuntimeModuleCount++;
    RtlReleaseSRWLockExclusive(&WerRuntimeLock);
    return S_OK;
}

HRESULT
WINAPI
WerUnregisterRuntimeExceptionModule(
    _In_ PCWSTR DllName,
    _In_opt_ PVOID Context)
{
    PBASE_WER_REGISTRATION Registration;
    PWER_RUNTIME_MODULE Module;
    UNICODE_STRING Name;
    PULONG64 Link;

    if (!DllName) return E_INVALIDARG;
    if (BaseWerGetState() & BASE_WER_RECOVERY_IN_PROGRESS) return WER_E_INVALID_STATE;
    RtlInitUnicodeString(&Name, DllName);
    Registration = BaseWerGetRegistration(FALSE);
    if (!Registration) return WER_E_NOT_FOUND;

    RtlAcquireSRWLockExclusive(&WerRuntimeLock);
    Link = FindRuntimeModule(Registration, &Name, Context);
    if (!Link)
    {
        RtlReleaseSRWLockExclusive(&WerRuntimeLock);
        return WER_E_NOT_FOUND;
    }

    Module = (PWER_RUNTIME_MODULE)(ULONG_PTR)*Link;
    InterlockedExchange64((PLONG64)Link, (LONG64)Module->Shared.Next);
    WerRuntimeModuleCount--;
    RtlReleaseSRWLockExclusive(&WerRuntimeLock);
    RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Module);
    return S_OK;
}

VOID
WerCleanupRuntimeExceptionModules(VOID)
{
    PBASE_WER_REGISTRATION Registration = BaseWerGetRegistration(FALSE);
    PWER_RUNTIME_MODULE Module;

    if (!Registration) return;

    RtlAcquireSRWLockExclusive(&WerRuntimeLock);
    while (Registration->RuntimeModules)
    {
        Module = (PWER_RUNTIME_MODULE)(ULONG_PTR)Registration->RuntimeModules;
        InterlockedExchange64((PLONG64)&Registration->RuntimeModules, (LONG64)Module->Shared.Next);
        RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, Module);
    }
    WerRuntimeModuleCount = 0;
    RtlReleaseSRWLockExclusive(&WerRuntimeLock);
}
