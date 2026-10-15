/*
 * PROJECT:     LiberNT 'Layers' Shim library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VerifyVersionInfoLite shim
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <shimlib.h>


#define SHIM_NS         VerifyVersionInfoLite
#include <setup_shim.inl>

BOOL WINAPI SHIM_OBJ_NAME(APIHook_VerifyVersionInfoA)(LPOSVERSIONINFOEXA VersionInformation, DWORD TypeMask, DWORDLONG ConditionMask)
{
    return TRUE;
}

BOOL WINAPI SHIM_OBJ_NAME(APIHook_VerifyVersionInfoW)(LPOSVERSIONINFOEXW VersionInformation, DWORD TypeMask, DWORDLONG ConditionMask)
{
    return TRUE;
}

#define SHIM_NUM_HOOKS  2
#define SHIM_SETUP_HOOKS \
    SHIM_HOOK(0, "KERNEL32.DLL", "VerifyVersionInfoA", SHIM_OBJ_NAME(APIHook_VerifyVersionInfoA)) \
    SHIM_HOOK(1, "KERNEL32.DLL", "VerifyVersionInfoW", SHIM_OBJ_NAME(APIHook_VerifyVersionInfoW))

#include <implement_shim.inl>
