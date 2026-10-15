/*
 * PROJECT:     LiberNT 'General' Shim library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AddProcessParametersFlags shim
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <shimlib.h>
#include "ntndk.h"


#define SHIM_NS         AddProcessParametersFlags
#include <setup_shim.inl>

#define SHIM_NUM_HOOKS  0
#define SHIM_NOTIFY_FN SHIM_OBJ_NAME(Notify)

BOOL WINAPI SHIM_OBJ_NAME(Notify)(DWORD fdwReason, PVOID ptr)
{
    ULONG Flags = 0;

    if (fdwReason == SHIM_REASON_INIT &&
        NT_SUCCESS(RtlCharToInteger(SHIM_OBJ_NAME(g_szCommandLine), 16, &Flags)))
    {
        NtCurrentPeb()->ProcessParameters->Flags |= Flags;
    }
    return TRUE;
}

#include <implement_shim.inl>
