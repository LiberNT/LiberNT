/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Shared code of the Linux program launchers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _LX_STRINGS
{
    PCHAR Buffer;
    ULONG Length;
    ULONG Count;
} LX_STRINGS, *PLX_STRINGS;

typedef struct _LX_LAUNCH
{
    PCWSTR Name;
    PCWSTR Root;
    PCSTR Cwd;
    PCSTR FileName;
    LX_STRINGS Args;
    LX_STRINGS Env;
    ULONG Flags;
    BOOL Terminal;
} LX_LAUNCH, *PLX_LAUNCH;

PCHAR LxToUtf8(_In_ PCWSTR String);
BOOL LxStringsAdd(_Inout_ PLX_STRINGS Strings, _In_ PCSTR String);
BOOL LxEnvSet(_Inout_ PLX_STRINGS Env, _In_ PCSTR Assignment);
BOOL LxIsConsole(_In_ DWORD StdId);
int LxLaunch(_Inout_ PLX_LAUNCH Launch);
VOID LxKeepResolvConf(_In_ PCWSTR Root);
