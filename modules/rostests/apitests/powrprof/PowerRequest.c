/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power request objects: create, set and clear each documented request type
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>

typedef HANDLE (WINAPI *PPOWER_CREATE_REQUEST)(PREASON_CONTEXT);
typedef BOOL (WINAPI *PPOWER_CHANGE_REQUEST)(HANDLE, POWER_REQUEST_TYPE);

static const POWER_REQUEST_TYPE RequestTypes[] =
{
    PowerRequestDisplayRequired,
    PowerRequestSystemRequired,
    PowerRequestAwayModeRequired,
    PowerRequestExecutionRequired,
};

START_TEST(PowerRequest)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");
    PPOWER_CREATE_REQUEST pPowerCreateRequest = (PPOWER_CREATE_REQUEST)GetProcAddress(Kernel32, "PowerCreateRequest");
    PPOWER_CHANGE_REQUEST pPowerSetRequest = (PPOWER_CHANGE_REQUEST)GetProcAddress(Kernel32, "PowerSetRequest");
    PPOWER_CHANGE_REQUEST pPowerClearRequest = (PPOWER_CHANGE_REQUEST)GetProcAddress(Kernel32, "PowerClearRequest");
    REASON_CONTEXT Context = {0};
    HANDLE Request, Duplicate;
    BOOL Success;
    ULONG i;

    if (!pPowerCreateRequest || !pPowerSetRequest || !pPowerClearRequest)
    {
        skip("Power request functions are not exported\n");
        return;
    }

    Context.Version = POWER_REQUEST_CONTEXT_VERSION;
    Context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    Context.Reason.SimpleReasonString = L"LiberNT power request test";

    SetLastError(0xdeadbeef);
    Request = pPowerCreateRequest(&Context);
    ok(Request != NULL && Request != INVALID_HANDLE_VALUE, "PowerCreateRequest failed: handle %p, error %lu\n", Request, GetLastError());
    if (Request == NULL || Request == INVALID_HANDLE_VALUE)
        return;

    for (i = 0; i < ARRAYSIZE(RequestTypes); i++)
    {
        winetest_push_context("type %d", RequestTypes[i]);
        SetLastError(0xdeadbeef);
        Success = pPowerSetRequest(Request, RequestTypes[i]);
        ok(Success, "PowerSetRequest failed: %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        Success = pPowerSetRequest(Request, RequestTypes[i]);
        ok(Success, "second PowerSetRequest failed: %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        Success = pPowerClearRequest(Request, RequestTypes[i]);
        ok(Success, "PowerClearRequest failed: %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        Success = pPowerClearRequest(Request, RequestTypes[i]);
        ok(Success, "second PowerClearRequest failed: %lu\n", GetLastError());
        winetest_pop_context();
    }

    SetLastError(0xdeadbeef);
    Success = pPowerClearRequest(Request, PowerRequestSystemRequired);
    ok(!Success, "PowerClearRequest of a request type that is not set succeeded\n");
    ok_long(GetLastError(), ERROR_NOT_SUPPORTED);

    SetLastError(0xdeadbeef);
    Success = pPowerSetRequest(Request, (POWER_REQUEST_TYPE)0x100);
    ok(!Success, "PowerSetRequest with an unknown request type succeeded\n");
    ok_long(GetLastError(), ERROR_NOT_SUPPORTED);

    Duplicate = NULL;
    SetLastError(0xdeadbeef);
    Success = DuplicateHandle(GetCurrentProcess(), Request, GetCurrentProcess(), &Duplicate,
                              0, FALSE, DUPLICATE_SAME_ACCESS);
    ok(!Success, "DuplicateHandle of a power request succeeded\n");
    ok_long(GetLastError(), ERROR_ACCESS_DENIED);
    if (Success) CloseHandle(Duplicate);

    SetLastError(0xdeadbeef);
    Success = pPowerSetRequest(GetCurrentProcess(), PowerRequestSystemRequired);
    ok(!Success, "PowerSetRequest on a process handle succeeded\n");

    ok(CloseHandle(Request), "CloseHandle failed: %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    Success = pPowerSetRequest(Request, PowerRequestSystemRequired);
    ok(!Success, "PowerSetRequest on a closed handle succeeded\n");
}
