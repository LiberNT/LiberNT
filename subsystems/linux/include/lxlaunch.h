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

typedef struct _LX_ACCOUNT
{
    CHAR Name[64];
    ULONG Uid;
    ULONG Gid;
    CHAR Home[MAX_PATH];
    CHAR Shell[MAX_PATH];
    ULONG GroupCount;
    ULONG Groups[32];
} LX_ACCOUNT, *PLX_ACCOUNT;

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
    BOOL Detached;
    PCSTR Input;
    BOOL User;
    BOOL RuntimeDir;
    BOOL SearchPath;
    ULONG Uid;
    ULONG Gid;
    ULONG GroupCount;
    ULONG Groups[32];
} LX_LAUNCH, *PLX_LAUNCH;

PCHAR LxToUtf8(_In_ PCWSTR String);
BOOL LxStringsAdd(_Inout_ PLX_STRINGS Strings, _In_ PCSTR String);
BOOL LxEnvSet(_Inout_ PLX_STRINGS Env, _In_ PCSTR Assignment);
BOOL LxIsConsole(_In_ DWORD StdId);
int LxLaunch(_Inout_ PLX_LAUNCH Launch);
BOOL LxRootHasFile(_In_ PCWSTR Root, _In_ PCWSTR Relative);
BOOL LxRootDefaultUser(_In_ PCWSTR Root, _Out_writes_z_(Size) PCHAR Name, _In_ SIZE_T Size);
BOOL LxRootGroupId(_In_ PCWSTR Root, _In_ PCSTR Name, _Out_ PULONG Gid);
BOOL LxRootAccount(_In_ PCWSTR Root, _In_opt_ PCSTR Name, _In_ ULONG Uid, _Out_ PLX_ACCOUNT Account);
BOOL LxSetSession(_Inout_ PLX_LAUNCH Launch, _In_ PCWSTR Root, _In_ PLX_ACCOUNT Account);
VOID LxKeepResolvConf(_In_ PCWSTR Root);
