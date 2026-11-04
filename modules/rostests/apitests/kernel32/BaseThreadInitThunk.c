/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Check the BaseThreadInitThunk calling convention
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "precomp.h"

typedef VOID (FASTCALL *BASE_THREAD_INIT_THUNK)(DWORD, LPTHREAD_START_ROUTINE, LPVOID);

static BASE_THREAD_INIT_THUNK Thunk;
static LONG Called;
static ULONG_PTR Received;

static DWORD WINAPI
Target(LPVOID Parameter)
{
    Received = (ULONG_PTR)Parameter;
    InterlockedIncrement(&Called);
    return 0x13579;
}

static DWORD WINAPI
InvokeThunk(LPVOID Parameter)
{
    Thunk(0, Target, Parameter);
    return 0;
}

START_TEST(BaseThreadInitThunk)
{
    HANDLE Thread;
    DWORD Code = 0;
    DWORD Wait;
    ULONG_PTR Parameter = 0x2468;

    Thunk = (BASE_THREAD_INIT_THUNK)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "BaseThreadInitThunk");
    if (!Thunk)
    {
        skip("BaseThreadInitThunk is unavailable\n");
        return;
    }

    Thread = CreateThread(NULL, 0, InvokeThunk, (LPVOID)Parameter, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread) return;
    Wait = WaitForSingleObject(Thread, 5000);
    ok(Wait == WAIT_OBJECT_0, "Wait returned %lu\n", Wait);
    if (Wait == WAIT_OBJECT_0)
    {
        ok(GetExitCodeThread(Thread, &Code), "GetExitCodeThread failed: %lu\n", GetLastError());
        ok(Code == 0x13579, "Exit code is %#lx\n", Code);
        ok(Called == 1, "Target called %ld times\n", Called);
        ok(Received == Parameter, "Parameter is %p\n", (void *)Received);
    }
    CloseHandle(Thread);
}
