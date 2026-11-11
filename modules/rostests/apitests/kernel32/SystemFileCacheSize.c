/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for GetSystemFileCacheSize and SetSystemFileCacheSize
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <ndk/setypes.h>

#define FILE_CACHE_VALID_FLAGS (FILE_CACHE_MAX_HARD_ENABLE | FILE_CACHE_MAX_HARD_DISABLE | \
                                FILE_CACHE_MIN_HARD_ENABLE | FILE_CACHE_MIN_HARD_DISABLE)

typedef BOOL (WINAPI *FN_GetSystemFileCacheSize)(PSIZE_T, PSIZE_T, PDWORD);
typedef BOOL (WINAPI *FN_SetSystemFileCacheSize)(SIZE_T, SIZE_T, DWORD);

static FN_GetSystemFileCacheSize pGetSystemFileCacheSize;
static FN_SetSystemFileCacheSize pSetSystemFileCacheSize;

static
VOID
TestInvalidFlags(
    _In_ SIZE_T Minimum,
    _In_ SIZE_T Maximum)
{
    static const DWORD Invalid[] =
    {
        FILE_CACHE_MAX_HARD_ENABLE | FILE_CACHE_MAX_HARD_DISABLE,
        FILE_CACHE_MIN_HARD_ENABLE | FILE_CACHE_MIN_HARD_DISABLE,
        FILE_CACHE_VALID_FLAGS,
        0x10,
        0x80000000,
    };
    ULONG Index;
    BOOL Result;

    for (Index = 0; Index < ARRAYSIZE(Invalid); Index++)
    {
        SetLastError(0xdeadbeef);
        Result = pSetSystemFileCacheSize(Minimum, Maximum, Invalid[Index]);
        ok(Result == FALSE, "Flags 0x%lx: returned %d\n", Invalid[Index], Result);
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "Flags 0x%lx: last error %lu\n",
           Invalid[Index], GetLastError());
    }
}

START_TEST(SystemFileCacheSize)
{
    SIZE_T Minimum = (SIZE_T)-2;
    SIZE_T Maximum = (SIZE_T)-2;
    SIZE_T Minimum2;
    SIZE_T Maximum2;
    DWORD Flags = 0xdeadbeef;
    DWORD Flags2;
    BOOLEAN WasEnabled;
    BOOLEAN Dummy;
    NTSTATUS Status;
    BOOL Result;

    pGetSystemFileCacheSize = (FN_GetSystemFileCacheSize)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                        "GetSystemFileCacheSize");
    pSetSystemFileCacheSize = (FN_SetSystemFileCacheSize)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                        "SetSystemFileCacheSize");
    ok(pGetSystemFileCacheSize != NULL, "GetSystemFileCacheSize is missing\n");
    ok(pSetSystemFileCacheSize != NULL, "SetSystemFileCacheSize is missing\n");
    if (pGetSystemFileCacheSize == NULL || pSetSystemFileCacheSize == NULL)
        return;

    SetLastError(0xdeadbeef);
    Result = pGetSystemFileCacheSize(&Minimum, &Maximum, &Flags);
    ok_eq_bool(Result, TRUE);
    ok_eq_ulong(GetLastError(), 0xdeadbeef);
    if (!Result)
        return;

    ok(Minimum != (SIZE_T)-2, "Minimum was not written\n");
    ok(Maximum != (SIZE_T)-2, "Maximum was not written\n");
    ok(Minimum <= Maximum, "Minimum 0x%I64x > maximum 0x%I64x\n", (ULONGLONG)Minimum, (ULONGLONG)Maximum);
    ok((Flags & ~(FILE_CACHE_MAX_HARD_ENABLE | FILE_CACHE_MIN_HARD_ENABLE)) == 0, "Flags 0x%lx\n", Flags);

    TestInvalidFlags(Minimum, Maximum);

    Status = RtlAdjustPrivilege(SE_INCREASE_QUOTA_PRIVILEGE, FALSE, FALSE, &WasEnabled);
    ok(NT_SUCCESS(Status), "RtlAdjustPrivilege(disable) returned 0x%08lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;

    SetLastError(0xdeadbeef);
    Result = pSetSystemFileCacheSize(Minimum, Maximum, 0);
    ok_eq_bool(Result, FALSE);
    ok_eq_ulong(GetLastError(), ERROR_PRIVILEGE_NOT_HELD);

    Status = RtlAdjustPrivilege(SE_INCREASE_QUOTA_PRIVILEGE, TRUE, FALSE, &Dummy);
    ok(NT_SUCCESS(Status), "RtlAdjustPrivilege(enable) returned 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        SetLastError(0xdeadbeef);
        Result = pSetSystemFileCacheSize(Minimum, Maximum, 0);
        ok_eq_bool(Result, TRUE);
        ok_eq_ulong(GetLastError(), 0xdeadbeef);

        Result = pGetSystemFileCacheSize(&Minimum2, &Maximum2, &Flags2);
        ok_eq_bool(Result, TRUE);
        ok_eq_size(Minimum2, Minimum);
        ok_eq_size(Maximum2, Maximum);
        ok_eq_ulong(Flags2, Flags);
    }

    RtlAdjustPrivilege(SE_INCREASE_QUOTA_PRIVILEGE, WasEnabled, FALSE, &Dummy);
}
