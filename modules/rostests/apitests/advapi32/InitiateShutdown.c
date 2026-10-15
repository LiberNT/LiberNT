/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests InitiateShutdownW, InitiateSystemShutdownExW and AbortSystemShutdownW
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

typedef DWORD (WINAPI *PFN_INITIATESHUTDOWNW)(LPWSTR, LPWSTR, DWORD, DWORD, DWORD);

static PFN_INITIATESHUTDOWNW pInitiateShutdownW;

static BOOL
SetShutdownPrivilege(BOOL Enable)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE Token;
    BOOL Result;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &Token))
        return FALSE;

    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : 0;
    Result = LookupPrivilegeValueW(NULL, L"SeShutdownPrivilege", &Privileges.Privileges[0].Luid) &&
             AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL) &&
             GetLastError() == ERROR_SUCCESS;
    CloseHandle(Token);
    return Result;
}

static VOID
AbortScheduled(PCSTR Tag)
{
    BOOL Ret;

    SetLastError(0xdeadbeef);
    Ret = AbortSystemShutdownW(NULL);
    ok(Ret, "%s: abort failed with %lu\n", Tag, GetLastError());
}

static VOID
TestWithoutPrivilege(VOID)
{
    DWORD Error;
    BOOL Ret;

    Error = pInitiateShutdownW(NULL, NULL, 600, SHUTDOWN_RESTART, SHTDN_REASON_FLAG_PLANNED);
    ok(Error == ERROR_ACCESS_DENIED, "InitiateShutdownW returned %lu\n", Error);
    if (Error == ERROR_SUCCESS)
        AbortScheduled("no privilege");

    SetLastError(0xdeadbeef);
    Ret = InitiateSystemShutdownExW(NULL, NULL, 600, FALSE, TRUE, SHTDN_REASON_FLAG_PLANNED);
    ok(!Ret, "InitiateSystemShutdownExW succeeded\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "InitiateSystemShutdownExW error %lu\n", GetLastError());
    if (Ret)
        AbortScheduled("no privilege ex");

    SetLastError(0xdeadbeef);
    Ret = AbortSystemShutdownW(NULL);
    ok(!Ret, "AbortSystemShutdownW succeeded\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED, "AbortSystemShutdownW error %lu\n", GetLastError());
}

static VOID
TestWithPrivilege(VOID)
{
    WCHAR Message[513];
    DWORD Error;
    BOOL Ret;
    ULONG i;

    SetLastError(0xdeadbeef);
    Error = pInitiateShutdownW(NULL, NULL, 315360000, SHUTDOWN_RESTART, SHTDN_REASON_FLAG_PLANNED);
    ok(Error == ERROR_INVALID_PARAMETER, "decade grace returned %lu\n", Error);
    ok(GetLastError() == 0xdeadbeef, "decade grace error %lu\n", GetLastError());
    if (Error == ERROR_SUCCESS)
        AbortScheduled("decade");

    for (i = 0; i < 512; i++)
        Message[i] = L'a';
    Message[512] = UNICODE_NULL;
    SetLastError(0xdeadbeef);
    Error = pInitiateShutdownW(NULL, Message, 600, SHUTDOWN_RESTART, SHTDN_REASON_FLAG_PLANNED);
    ok(Error == ERROR_INVALID_PARAMETER, "512 character message returned %lu\n", Error);
    ok(GetLastError() == 0xdeadbeef, "512 character message error %lu\n", GetLastError());
    if (Error == ERROR_SUCCESS)
        AbortScheduled("512");

    Message[511] = UNICODE_NULL;
    SetLastError(0xdeadbeef);
    Error = pInitiateShutdownW(NULL, Message, 600, SHUTDOWN_RESTART, SHTDN_REASON_FLAG_PLANNED);
    ok(Error == ERROR_SUCCESS, "511 character message returned %lu\n", Error);
    ok(GetLastError() == ERROR_SUCCESS, "511 character message error %lu\n", GetLastError());
    if (Error == ERROR_SUCCESS)
        AbortScheduled("511");

    Error = pInitiateShutdownW(NULL, NULL, 600, 0x80000000, SHTDN_REASON_FLAG_PLANNED);
    ok(Error == ERROR_SUCCESS, "unknown flag returned %lu\n", Error);
    if (Error == ERROR_SUCCESS)
        AbortScheduled("unknown flag");

    Error = pInitiateShutdownW(NULL, NULL, 600, SHUTDOWN_RESTART, SHTDN_REASON_FLAG_PLANNED);
    ok(Error == ERROR_SUCCESS, "schedule returned %lu\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        Error = pInitiateShutdownW(NULL, NULL, 600, SHUTDOWN_POWEROFF, SHTDN_REASON_FLAG_PLANNED);
        ok(Error == ERROR_SHUTDOWN_IS_SCHEDULED, "second schedule returned %lu\n", Error);
        SetLastError(0xdeadbeef);
        Ret = InitiateSystemShutdownExW(NULL, NULL, 600, FALSE, TRUE, SHTDN_REASON_FLAG_PLANNED);
        ok(!Ret, "InitiateSystemShutdownExW succeeded while scheduled\n");
        ok(GetLastError() == ERROR_SHUTDOWN_IS_SCHEDULED, "InitiateSystemShutdownExW error %lu\n", GetLastError());
        AbortScheduled("scheduled");
    }

    SetLastError(0xdeadbeef);
    Ret = AbortSystemShutdownW(NULL);
    ok(!Ret, "idle abort succeeded\n");
    ok(GetLastError() == ERROR_NO_SHUTDOWN_IN_PROGRESS, "idle abort error %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Ret = InitiateSystemShutdownExW(NULL, NULL, 315360000, FALSE, TRUE, SHTDN_REASON_FLAG_PLANNED);
    ok(!Ret, "decade InitiateSystemShutdownExW succeeded\n");
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "decade InitiateSystemShutdownExW error %lu\n", GetLastError());
    if (Ret)
        AbortScheduled("decade ex");
}

START_TEST(InitiateShutdown)
{
    pInitiateShutdownW = (PFN_INITIATESHUTDOWNW)GetProcAddress(GetModuleHandleW(L"advapi32.dll"), "InitiateShutdownW");
    ok(pInitiateShutdownW != NULL, "InitiateShutdownW is missing\n");
    if (!pInitiateShutdownW)
        return;

    if (!SetShutdownPrivilege(TRUE))
    {
        skip("The caller does not hold SeShutdownPrivilege\n");
        return;
    }

    SetShutdownPrivilege(FALSE);
    TestWithoutPrivilege();
    SetShutdownPrivilege(TRUE);
    TestWithPrivilege();
    SetShutdownPrivilege(FALSE);
}
