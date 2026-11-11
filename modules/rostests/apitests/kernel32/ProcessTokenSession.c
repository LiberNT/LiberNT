/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     A process started with a token of another session runs in that session
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/pstypes.h>
#include <ndk/psfuncs.h>

START_TEST(ProcessTokenSession)
{
    WCHAR Application[MAX_PATH];
    PROCESS_BASIC_INFORMATION Basic;
    PROCESS_INFORMATION Info;
    STARTUPINFOW Startup;
    HANDLE Base = NULL, Token = NULL;
    DWORD Own = 0, Target, Child = 0, PebSession = 0;
    SIZE_T Read = 0;
    NTSTATUS Status;

    ok(ProcessIdToSessionId(GetCurrentProcessId(), &Own), "ProcessIdToSessionId failed %lu\n", GetLastError());
    Target = WTSGetActiveConsoleSessionId();
    if (Target == 0xFFFFFFFF || Target == Own)
    {
        skip("no other session to start a process in (own %lu, console %lu)\n", Own, Target);
        return;
    }

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &Base) ||
        !DuplicateTokenEx(Base, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &Token) ||
        !SetTokenInformation(Token, TokenSessionId, &Target, sizeof(Target)))
    {
        skip("cannot make a token of session %lu: %lu\n", Target, GetLastError());
        if (Token) CloseHandle(Token);
        if (Base) CloseHandle(Base);
        return;
    }

    ZeroMemory(&Info, sizeof(Info));
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application));
    ok(CreateProcessAsUserW(Token, Application, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &Startup, &Info),
       "CreateProcessAsUser failed %lu\n", GetLastError());
    if (Info.hProcess)
    {
        ok(ProcessIdToSessionId(Info.dwProcessId, &Child) && Child == Target,
           "child runs in session %lu, its token in %lu\n", Child, Target);

        Status = NtQueryInformationProcess(Info.hProcess, ProcessBasicInformation, &Basic, sizeof(Basic), NULL);
        ok(NT_SUCCESS(Status) &&
           ReadProcessMemory(Info.hProcess, (PUCHAR)Basic.PebBaseAddress + FIELD_OFFSET(PEB, SessionId),
                             &PebSession, sizeof(PebSession), &Read) &&
           PebSession == Target,
           "child PEB says session %lu (0x%lx)\n", PebSession, Status);

        TerminateProcess(Info.hProcess, 0);
        CloseHandle(Info.hThread);
        CloseHandle(Info.hProcess);
    }

    CloseHandle(Token);
    CloseHandle(Base);
}
