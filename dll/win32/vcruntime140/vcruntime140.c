/*
 * PROJECT:     LiberNT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VC++ 2015+ runtime entry points that ucrtbase does not provide
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>

BOOL
WINAPI
DllMainCRTStartup(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Reserved);
    return TRUE;
}

VOID
CDECL
__telemetry_main_invoke_trigger(
    _In_ HINSTANCE Instance)
{
    UNREFERENCED_PARAMETER(Instance);
}

VOID
CDECL
__telemetry_main_return_trigger(
    _In_ HINSTANCE Instance)
{
    UNREFERENCED_PARAMETER(Instance);
}

BOOL
CDECL
__vcrt_InitializeCriticalSectionEx(
    _Out_ CRITICAL_SECTION *Section,
    _In_ DWORD SpinCount,
    _In_ DWORD Flags)
{
    return InitializeCriticalSectionEx(Section, SpinCount, Flags);
}

DWORD
CDECL
__vcrt_GetModuleFileNameW(
    _In_opt_ HMODULE Module,
    _Out_writes_to_(Size, return) LPWSTR Name,
    _In_ DWORD Size)
{
    return GetModuleFileNameW(Module, Name, Size);
}

HMODULE
CDECL
__vcrt_GetModuleHandleW(
    _In_opt_ LPCWSTR Name)
{
    return GetModuleHandleW(Name);
}

HMODULE
CDECL
__vcrt_LoadLibraryExW(
    _In_ LPCWSTR Name,
    _In_opt_ HANDLE File,
    _In_ DWORD Flags)
{
    return LoadLibraryExW(Name, File, Flags);
}
