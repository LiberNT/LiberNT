/*
 * PROJECT:     LiberNT KernelBase
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Module handle lookup by address
 * COPYRIGHT:   Copyright 2026 LuRy <lury@lury.cz>
 */

#include <windows.h>
#include <winternl.h>

NTSYSAPI NTSTATUS NTAPI LdrLockLoaderLock(_In_ ULONG Flags, _Out_opt_ PULONG Disposition, _Out_ PULONG_PTR Cookie);
NTSYSAPI NTSTATUS NTAPI LdrUnlockLoaderLock(_In_ ULONG Flags, _In_opt_ ULONG_PTR Cookie);

/* Invalid flags go to kernel32 unchanged, which rejects them before any lookup */
static BOOL
KbValidModuleHandleFlags(_In_ DWORD dwFlags)
{
    const DWORD Valid = GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |
                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS;
    const DWORD Exclusive = GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;

    return !(dwFlags & ~Valid) && (dwFlags & Exclusive) != Exclusive;
}

BOOL
WINAPI
KbGetModuleHandleExW(
    _In_ DWORD dwFlags,
    _In_opt_ LPCWSTR lpModuleName,
    _Out_ HMODULE *phModule)
{
    PVOID ImageBase = NULL;
    ULONG_PTR Cookie;
    NTSTATUS Status;
    BOOL Result;

    if (!KbValidModuleHandleFlags(dwFlags) || !(dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) ||
        !lpModuleName || !phModule)
    {
        return GetModuleHandleExW(dwFlags, lpModuleName, phModule);
    }

    Status = LdrLockLoaderLock(0, NULL, &Cookie);
    if (!NT_SUCCESS(Status))
    {
        *phModule = NULL;
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }
    if (RtlPcToFileHeader((PVOID)lpModuleName, &ImageBase))
    {
        Result = GetModuleHandleExW(dwFlags, (LPCWSTR)ImageBase, phModule);
    }
    else
    {
        *phModule = NULL;
        SetLastError(ERROR_MOD_NOT_FOUND);
        Result = FALSE;
    }
    LdrUnlockLoaderLock(0, Cookie);
    return Result;
}

BOOL
WINAPI
KbGetModuleHandleExA(
    _In_ DWORD dwFlags,
    _In_opt_ LPCSTR lpModuleName,
    _Out_ HMODULE *phModule)
{
    if (dwFlags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)
        return KbGetModuleHandleExW(dwFlags, (LPCWSTR)lpModuleName, phModule);

    return GetModuleHandleExA(dwFlags, lpModuleName, phModule);
}
