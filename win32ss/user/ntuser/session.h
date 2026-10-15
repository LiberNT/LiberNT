/*
 * PROJECT:     LiberNT Win32k subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Per-session USER state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _USER_SESSION
{
    struct _USER_SESSION *Next;
    ULONG SessionId;
    PEPROCESS CsrProcess;
    PVOID CsrApiPort;
    DWORD PendingSystemThreads;
    HANDLE LogonPid;
    UNICODE_STRING WinStaDirName;
    HANDLE WinStaDir;
    struct _WINSTATION_OBJECT *InputWindowStation;
    PKEVENT DesktopSwitchEvent;
    HANDLE DesktopSwitchEventHandle;
    struct _THREADINFO *DesktopThread;
    KEVENT DesktopThreadStarted;
} USER_SESSION, *PUSER_SESSION;

extern PUSER_SESSION gpDisplaySession;

PUSER_SESSION FASTCALL UserFindSession(ULONG SessionId);
PUSER_SESSION FASTCALL UserGetProcessSession(PEPROCESS Process);
PUSER_SESSION FASTCALL UserGetCurrentSession(VOID);
NTSTATUS FASTCALL UserCreateSession(ULONG SessionId, PUSER_SESSION *Session);
PUNICODE_STRING FASTCALL UserGetWinStaDirName(PEPROCESS Process);
struct _WINSTATION_OBJECT *FASTCALL UserGetInputWindowStation(PEPROCESS Process);
struct _WINSTATION_OBJECT *FASTCALL UserGetDisplayInputWindowStation(VOID);
BOOLEAN FASTCALL UserIsCsrProcess(PEPROCESS Process);
BOOLEAN FASTCALL UserIsLogonProcess(HANDLE ProcessId);
BOOLEAN FASTCALL UserIsDesktopThread(struct _THREADINFO *pti);
struct _THREADINFO *FASTCALL UserGetDesktopThread(VOID);
