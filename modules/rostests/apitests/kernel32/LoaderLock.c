/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test which module queries complete while another thread holds the loader lock
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef NTSTATUS (NTAPI *PLDR_LOCK_LOADER_LOCK)(ULONG, PULONG, PULONG_PTR);
typedef NTSTATUS (NTAPI *PLDR_UNLOCK_LOADER_LOCK)(ULONG, ULONG_PTR);

typedef struct _LOADER_LOCK_JOB
{
    BOOL (*Call)(void);
    HANDLE Ready;
    HANDLE Go;
    HANDLE Done;
    BOOL Result;
} LOADER_LOCK_JOB;

static BOOL CallGetProcAddress(void)
{
    return GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "Sleep") != NULL;
}

static BOOL CallGetProcAddressForwarder(void)
{
    return GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "HeapAlloc") != NULL;
}

static BOOL CallGetProcAddressMissing(void)
{
    SetLastError(0xdeadbeef);
    return GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoaderLockTestNoSuchExport") == NULL &&
           GetLastError() == ERROR_PROC_NOT_FOUND;
}

static BOOL CallGetModuleHandle(void)
{
    return GetModuleHandleW(L"ntdll.dll") != NULL;
}

static BOOL CallGetModuleHandleMissing(void)
{
    SetLastError(0xdeadbeef);
    return GetModuleHandleW(L"loaderlocktestnosuchdll.dll") == NULL && GetLastError() == ERROR_MOD_NOT_FOUND;
}

static BOOL CallGetModuleHandleExAddress(void)
{
    HMODULE Module = NULL;

    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCWSTR)CallGetModuleHandleExAddress, &Module) &&
           Module == GetModuleHandleW(NULL);
}

static BOOL CallGetModuleFileName(void)
{
    WCHAR Name[MAX_PATH];

    return GetModuleFileNameW(NULL, Name, RTL_NUMBER_OF(Name)) != 0 &&
           GetModuleFileNameW(GetModuleHandleW(L"kernel32.dll"), Name, RTL_NUMBER_OF(Name)) != 0;
}

static BOOL CallRaiseException(void)
{
    BOOL Handled = FALSE;

    _SEH2_TRY
    {
        RaiseException(0xE0004C4C, 0, 0, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Handled = TRUE;
    }
    _SEH2_END;

    return Handled;
}

static BOOL CallLoadLibraryLoaded(void)
{
    HMODULE Module = LoadLibraryW(L"kernel32.dll");

    if (!Module)
        return FALSE;
    FreeLibrary(Module);
    return TRUE;
}

static BOOL CallLoadLibraryNew(void)
{
    HMODULE Module = LoadLibraryW(L"mpr.dll");

    if (!Module)
        return FALSE;
    FreeLibrary(Module);
    return TRUE;
}

static DWORD WINAPI LoaderLockWorker(PVOID Context)
{
    LOADER_LOCK_JOB *Job = Context;

    SetEvent(Job->Ready);
    if (WaitForSingleObject(Job->Go, 10000) != WAIT_OBJECT_0)
        return 1;
    Job->Result = Job->Call();
    SetEvent(Job->Done);
    return 0;
}

static void TestCall(
    PLDR_LOCK_LOADER_LOCK Lock,
    PLDR_UNLOCK_LOADER_LOCK Unlock,
    BOOL (*Call)(void),
    const char *Name,
    BOOL CompletesWhileHeld)
{
    LOADER_LOCK_JOB Job = { Call, NULL, NULL, NULL, FALSE };
    ULONG_PTR Cookie = 0;
    ULONG State = 0;
    HANDLE Thread;
    DWORD WaitHeld, WaitReleased;
    NTSTATUS Status;

    Job.Ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    Job.Go = CreateEventW(NULL, TRUE, FALSE, NULL);
    Job.Done = CreateEventW(NULL, TRUE, FALSE, NULL);
    Thread = CreateThread(NULL, 0, LoaderLockWorker, &Job, 0, NULL);
    ok(Thread != NULL, "%s: CreateThread failed with %lu\n", Name, GetLastError());
    if (!Thread)
        return;

    ok(WaitForSingleObject(Job.Ready, 5000) == WAIT_OBJECT_0, "%s: worker did not start\n", Name);
    Status = Lock(0, &State, &Cookie);
    ok(Status == STATUS_SUCCESS, "%s: LdrLockLoaderLock returned 0x%lx\n", Name, Status);
    SetEvent(Job.Go);
    WaitHeld = WaitForSingleObject(Job.Done, 1000);
    Unlock(0, Cookie);
    WaitReleased = WaitForSingleObject(Job.Done, 5000);

    if (CompletesWhileHeld)
        ok(WaitHeld == WAIT_OBJECT_0, "%s: blocked while the loader lock was held\n", Name);
    else
        ok(WaitHeld == WAIT_TIMEOUT, "%s: completed while the loader lock was held (%lu)\n", Name, WaitHeld);
    ok(WaitReleased == WAIT_OBJECT_0, "%s: did not complete after the loader lock was released\n", Name);
    ok(Job.Result, "%s: call failed\n", Name);

    ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "%s: worker did not exit\n", Name);
    CloseHandle(Thread);
    CloseHandle(Job.Ready);
    CloseHandle(Job.Go);
    CloseHandle(Job.Done);
}

static void TestModuleFileNameLimits(void)
{
    WCHAR Name[MAX_PATH], Short[4];
    DWORD Length, Result;

    Length = GetModuleFileNameW(NULL, Name, RTL_NUMBER_OF(Name));
    ok(Length > RTL_NUMBER_OF(Short), "GetModuleFileName returned %lu\n", Length);

    memset(Short, 0xCC, sizeof(Short));
    SetLastError(0xdeadbeef);
    Result = GetModuleFileNameW(NULL, Short, RTL_NUMBER_OF(Short));
    ok(Result == RTL_NUMBER_OF(Short), "short buffer: returned %lu\n", Result);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "short buffer: error %lu\n", GetLastError());
    ok(Short[0] == Name[0] && Short[1] == Name[1] && Short[2] == Name[2], "short buffer: prefix not copied\n");
    ok(Short[3] == UNICODE_NULL, "short buffer: last character is 0x%04x\n", Short[3]);

    memset(Name, 0xCC, sizeof(Name));
    SetLastError(0xdeadbeef);
    Result = GetModuleFileNameW(NULL, Name, Length + 1);
    ok(Result == Length, "exact buffer: returned %lu, expected %lu\n", Result, Length);
    ok(GetLastError() == ERROR_SUCCESS, "exact buffer: error %lu\n", GetLastError());
    ok(Name[Length] == UNICODE_NULL, "exact buffer: not terminated\n");

    SetLastError(0xdeadbeef);
    Result = GetModuleFileNameW((HMODULE)(ULONG_PTR)0x10000, Name, RTL_NUMBER_OF(Name));
    ok(Result == 0, "unknown module: returned %lu\n", Result);
    ok(GetLastError() == ERROR_MOD_NOT_FOUND, "unknown module: error %lu\n", GetLastError());
}

START_TEST(LoaderLock)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PLDR_LOCK_LOADER_LOCK Lock = (PLDR_LOCK_LOADER_LOCK)GetProcAddress(Ntdll, "LdrLockLoaderLock");
    PLDR_UNLOCK_LOADER_LOCK Unlock = (PLDR_UNLOCK_LOADER_LOCK)GetProcAddress(Ntdll, "LdrUnlockLoaderLock");
    HMODULE Module;

    TestModuleFileNameLimits();

    ok(Lock != NULL && Unlock != NULL, "LdrLockLoaderLock/LdrUnlockLoaderLock not exported\n");
    if (!Lock || !Unlock)
        return;

    TestCall(Lock, Unlock, CallGetProcAddress, "GetProcAddress", TRUE);
    TestCall(Lock, Unlock, CallGetProcAddressForwarder, "GetProcAddress forwarder", TRUE);
    TestCall(Lock, Unlock, CallGetProcAddressMissing, "GetProcAddress missing", TRUE);
    TestCall(Lock, Unlock, CallGetModuleHandle, "GetModuleHandle", TRUE);
    TestCall(Lock, Unlock, CallGetModuleHandleMissing, "GetModuleHandle missing", TRUE);
    TestCall(Lock, Unlock, CallGetModuleHandleExAddress, "GetModuleHandleEx address", TRUE);
    TestCall(Lock, Unlock, CallGetModuleFileName, "GetModuleFileName", TRUE);
    TestCall(Lock, Unlock, CallRaiseException, "RaiseException", TRUE);
    TestCall(Lock, Unlock, CallLoadLibraryLoaded, "LoadLibrary loaded", TRUE);

    if (GetModuleHandleW(L"mpr.dll"))
    {
        skip("mpr.dll is already loaded\n");
        return;
    }

    TestCall(Lock, Unlock, CallLoadLibraryNew, "LoadLibrary new", FALSE);

    Module = LoadLibraryW(L"mpr.dll");
    ok(Module != NULL, "LoadLibrary(mpr.dll) failed with %lu\n", GetLastError());
    if (!Module)
        return;
    TestCall(Lock, Unlock, CallLoadLibraryNew, "LoadLibrary and FreeLibrary loaded unpinned", TRUE);
    ok(GetModuleHandleW(L"mpr.dll") == Module, "mpr.dll was unloaded while still referenced\n");
    FreeLibrary(Module);
}
