/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests RtlWow64EnableFsRedirectionEx in native and WoW64 callers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef NTSTATUS (NTAPI *PFN_ENABLE_FS_REDIRECTION_EX)(PVOID, PVOID *);
typedef NTSTATUS (NTAPI *PFN_ENABLE_FS_REDIRECTION)(BOOLEAN);

START_TEST(RtlWow64EnableFsRedirectionEx)
{
    PFN_ENABLE_FS_REDIRECTION_EX pRtlWow64EnableFsRedirectionEx;
    PFN_ENABLE_FS_REDIRECTION pRtlWow64EnableFsRedirection;
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PVOID Original = NULL, Current;
    BOOL Wow64 = FALSE;
    NTSTATUS Status;

    pRtlWow64EnableFsRedirectionEx = (PFN_ENABLE_FS_REDIRECTION_EX)GetProcAddress(Ntdll, "RtlWow64EnableFsRedirectionEx");
    pRtlWow64EnableFsRedirection = (PFN_ENABLE_FS_REDIRECTION)GetProcAddress(Ntdll, "RtlWow64EnableFsRedirection");
    ok(pRtlWow64EnableFsRedirectionEx != NULL, "RtlWow64EnableFsRedirectionEx is missing\n");
    ok(pRtlWow64EnableFsRedirection != NULL, "RtlWow64EnableFsRedirection is missing\n");
    if (!pRtlWow64EnableFsRedirectionEx || !pRtlWow64EnableFsRedirection)
        return;

    if (!IsWow64Process(GetCurrentProcess(), &Wow64))
        Wow64 = FALSE;

    if (!Wow64)
    {
        Current = (PVOID)0xdeadbeef;
        Status = pRtlWow64EnableFsRedirectionEx((PVOID)FALSE, &Current);
        ok_ntstatus(Status, STATUS_NOT_IMPLEMENTED);
        Status = pRtlWow64EnableFsRedirectionEx((PVOID)TRUE, &Current);
        ok_ntstatus(Status, STATUS_NOT_IMPLEMENTED);
        return;
    }

    Status = pRtlWow64EnableFsRedirectionEx((PVOID)FALSE, &Original);
    ok_ntstatus(Status, STATUS_SUCCESS);

    Status = pRtlWow64EnableFsRedirectionEx((PVOID)FALSE, &Current);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Current == (PVOID)FALSE, "Previous level %p\n", Current);

    Status = pRtlWow64EnableFsRedirectionEx((PVOID)TRUE, &Current);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = pRtlWow64EnableFsRedirectionEx((PVOID)TRUE, &Current);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Current == (PVOID)TRUE, "Previous level %p\n", Current);

    Status = pRtlWow64EnableFsRedirection(TRUE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = pRtlWow64EnableFsRedirectionEx((PVOID)TRUE, &Current);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Current == (PVOID)FALSE, "Previous level %p\n", Current);

    Status = pRtlWow64EnableFsRedirectionEx((PVOID)123, &Current);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Current == (PVOID)TRUE, "Previous level %p\n", Current);

    Status = pRtlWow64EnableFsRedirectionEx((PVOID)0xdeadbeef, &Current);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Current == (PVOID)123, "Previous level %p\n", Current);

    Status = pRtlWow64EnableFsRedirectionEx((PVOID)TRUE, NULL);
    ok_ntstatus(Status, STATUS_ACCESS_VIOLATION);
    Status = pRtlWow64EnableFsRedirectionEx((PVOID)TRUE, (PVOID *)1);
    ok_ntstatus(Status, STATUS_ACCESS_VIOLATION);

    Status = pRtlWow64EnableFsRedirection(FALSE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    Status = pRtlWow64EnableFsRedirectionEx((PVOID)FALSE, &Current);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Current == (PVOID)TRUE, "Previous level %p\n", Current);

    pRtlWow64EnableFsRedirectionEx(Original, &Current);
}
