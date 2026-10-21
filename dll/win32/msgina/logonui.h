/*
 * PROJECT:     LiberNT Logon GINA DLL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Full-screen lock, sign-in, unlock and security options screens
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _LOGONUI_MODE
{
    LogonUiNotice,
    LogonUiLockedNotice,
    LogonUiSignIn,
    LogonUiUnlock,
    LogonUiOptions,
    LogonUiPower
} LOGONUI_MODE;

INT
LogonUiRun(
    _Inout_ PGINA_CONTEXT pgContext,
    _In_ LOGONUI_MODE Mode);

VOID
LogonUiRemoveCover(VOID);

BOOL
LogonUiCoverShown(VOID);

BOOL
LogonUiEverShown(VOID);

DWORD
LogonUiPowerDialog(
    _In_opt_ HWND Parent,
    _In_ DWORD AllowedOptions,
    _Inout_ PDWORD Action);

#ifdef __cplusplus
}
#endif
