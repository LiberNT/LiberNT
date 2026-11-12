/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for application recovery registration and the recovery of a crashing process
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <werapi.h>

#ifndef RECOVERY_DEFAULT_PING_INTERVAL
#define RECOVERY_DEFAULT_PING_INTERVAL 5000
#define RECOVERY_MAX_PING_INTERVAL     (5 * 60 * 1000)
#endif

#define CHILD_WAIT_MS       15000
#define CHILD_EXIT_RETURNED 0x600D
#define PING_INTERVAL_MS    100
#define PING_WORK_MS        (PING_INTERVAL_MS * 2 / 5)
#define HANG_INTERVAL_MS    PING_INTERVAL_MS

typedef HRESULT (WINAPI *FN_RegisterApplicationRecoveryCallback)(APPLICATION_RECOVERY_CALLBACK, PVOID, DWORD, DWORD);
typedef HRESULT (WINAPI *FN_UnregisterApplicationRecoveryCallback)(VOID);
typedef HRESULT (WINAPI *FN_GetApplicationRecoveryCallback)(HANDLE, APPLICATION_RECOVERY_CALLBACK *, PVOID *, PDWORD, PDWORD);
typedef HRESULT (WINAPI *FN_ApplicationRecoveryInProgress)(PBOOL);
typedef VOID (WINAPI *FN_ApplicationRecoveryFinished)(BOOL);
typedef HRESULT (WINAPI *FN_WerRegisterFile)(PCWSTR, WER_REGISTER_FILE_TYPE, DWORD);
typedef HRESULT (WINAPI *FN_WerRegisterMemoryBlock)(PVOID, DWORD);
typedef HRESULT (WINAPI *FN_WerRegisterRuntimeExceptionModule)(PCWSTR, PVOID);
typedef HRESULT (WINAPI *FN_WerSetFlags)(DWORD);
typedef HRESULT (WINAPI *FN_WerGetFlags)(HANDLE, PDWORD);

static FN_RegisterApplicationRecoveryCallback pRegisterApplicationRecoveryCallback;
static FN_UnregisterApplicationRecoveryCallback pUnregisterApplicationRecoveryCallback;
static FN_GetApplicationRecoveryCallback pGetApplicationRecoveryCallback;
static FN_ApplicationRecoveryInProgress pApplicationRecoveryInProgress;
static FN_ApplicationRecoveryFinished pApplicationRecoveryFinished;
static FN_WerRegisterFile pWerRegisterFile;
static FN_WerRegisterMemoryBlock pWerRegisterMemoryBlock;
static FN_WerRegisterRuntimeExceptionModule pWerRegisterRuntimeExceptionModule;
static FN_WerSetFlags pWerSetFlags;
static FN_WerGetFlags pWerGetFlags;

typedef struct _RECOVERY_SHARED
{
    HANDLE ReadyEvent;
    HANDLE DoneEvent;
    ULONG64 RegisteredCallback;
    ULONG64 RegisteredParameter;
    HRESULT Register;
    LONG CallbackCalls;
    ULONG64 Parameter;
    HRESULT InProgress;
    BOOL Cancelled;
    HRESULT InProgressNull;
    HRESULT InProgressSecond;
    HRESULT GetCallback;
    ULONG64 GetCallbackAddress;
    HRESULT RegisterFile;
    HRESULT RegisterMemoryBlock;
    HRESULT RegisterRuntimeModule;
    LONG Done;
    LONG AfterFinished;
    LONG AfterSleep;
} RECOVERY_SHARED, *PRECOVERY_SHARED;

typedef enum _RECOVERY_SLOT
{
    SlotRemote,
    SlotCrash,
    SlotHang,
    SlotPing,
    SlotReturn,
    SlotUnregistered,
    SlotFinished,
    SlotCount
} RECOVERY_SLOT;

static PRECOVERY_SHARED View;
static PRECOVERY_SHARED Shared;
static LONG *volatile CrashPointer;
static BYTE MemoryBlock[64];

static
DWORD
WINAPI
CrashCallback(
    _In_ PVOID Parameter)
{
    APPLICATION_RECOVERY_CALLBACK Callback = NULL;
    BOOL Cancelled = TRUE;

    InterlockedIncrement(&Shared->CallbackCalls);
    Shared->Parameter = (ULONG_PTR)Parameter;
    Shared->InProgress = pApplicationRecoveryInProgress(&Cancelled);
    Shared->Cancelled = Cancelled;
    Shared->InProgressNull = pApplicationRecoveryInProgress(NULL);
    Shared->GetCallback = pGetApplicationRecoveryCallback(GetCurrentProcess(), &Callback, NULL, NULL, NULL);
    Shared->GetCallbackAddress = (ULONG_PTR)Callback;
    Shared->RegisterFile = pWerRegisterFile(L"C:\\ReactOS\\recovery.txt", WerRegFileTypeOther, 0);
    Shared->RegisterMemoryBlock = pWerRegisterMemoryBlock(MemoryBlock, sizeof(MemoryBlock));
    Shared->RegisterRuntimeModule = pWerRegisterRuntimeExceptionModule(L"recovery_helper.dll", NULL);
    pApplicationRecoveryFinished(TRUE);
    InterlockedExchange(&Shared->AfterFinished, 1);
    return 0;
}

static
DWORD
WINAPI
HangCallback(
    _In_ PVOID Parameter)
{
    InterlockedIncrement(&Shared->CallbackCalls);
    Shared->Parameter = (ULONG_PTR)Parameter;
    Sleep(CHILD_WAIT_MS);
    InterlockedExchange(&Shared->AfterSleep, 1);
    return 0;
}

static
DWORD
WINAPI
PingCallback(
    _In_ PVOID Parameter)
{
    BOOL Cancelled = TRUE;

    InterlockedIncrement(&Shared->CallbackCalls);
    Shared->Parameter = (ULONG_PTR)Parameter;
    Sleep(PING_WORK_MS);
    Shared->InProgress = pApplicationRecoveryInProgress(&Cancelled);
    Shared->Cancelled = Cancelled;
    Sleep(PING_WORK_MS);
    Shared->InProgressSecond = pApplicationRecoveryInProgress(&Cancelled);
    Sleep(PING_WORK_MS);
    InterlockedExchange(&Shared->Done, 1);
    pApplicationRecoveryFinished(FALSE);
    InterlockedExchange(&Shared->AfterFinished, 1);
    return 0;
}

static
DWORD
WINAPI
ReturnCallback(
    _In_ PVOID Parameter)
{
    InterlockedIncrement(&Shared->CallbackCalls);
    Shared->Parameter = (ULONG_PTR)Parameter;
    return 0;
}

static
DWORD
WINAPI
CrashThread(
    _In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    *CrashPointer = 1;
    return 0;
}

static
VOID
CrashChild(
    _In_ APPLICATION_RECOVERY_CALLBACK Callback,
    _In_ DWORD PingInterval,
    _In_ BOOL Unregister)
{
    HANDLE Thread;

    Shared->RegisteredParameter = (ULONG_PTR)&Shared->Done;
    Shared->Register = pRegisterApplicationRecoveryCallback(Callback, &Shared->Done, PingInterval, 0);
    if (Unregister)
        pUnregisterApplicationRecoveryCallback();

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Thread = CreateThread(NULL, 0, CrashThread, NULL, 0, NULL);
    if (Thread)
        WaitForSingleObject(Thread, CHILD_WAIT_MS);
    ExitProcess(CHILD_EXIT_RETURNED);
}

static
VOID
RunChildMode(
    _In_ PCSTR Mode)
{
    if (!strcmp(Mode, "crash"))
        CrashChild(CrashCallback, 3000, FALSE);
    else if (!strcmp(Mode, "hang"))
        CrashChild(HangCallback, HANG_INTERVAL_MS, FALSE);
    else if (!strcmp(Mode, "ping"))
        CrashChild(PingCallback, PING_INTERVAL_MS, FALSE);
    else if (!strcmp(Mode, "return"))
        CrashChild(ReturnCallback, 3000, FALSE);
    else if (!strcmp(Mode, "unregistered"))
        CrashChild(ReturnCallback, 3000, TRUE);
    else if (!strcmp(Mode, "finished"))
        pApplicationRecoveryFinished(TRUE);
    else if (!strcmp(Mode, "remote") || !strcmp(Mode, "none"))
    {
        if (!strcmp(Mode, "remote"))
        {
            Shared->RegisteredCallback = (ULONG_PTR)ReturnCallback;
            Shared->RegisteredParameter = (ULONG_PTR)&Shared->Done;
            Shared->Register = pRegisterApplicationRecoveryCallback(ReturnCallback, &Shared->Done, 1234, 0);
            pWerSetFlags(WER_FAULT_REPORTING_FLAG_NOHEAP | WER_FAULT_REPORTING_FLAG_QUEUE);
        }
        SetEvent(Shared->ReadyEvent);
        WaitForSingleObject(Shared->DoneEvent, CHILD_WAIT_MS);
    }

    ExitProcess(CHILD_EXIT_RETURNED);
}

static
HANDLE
StartChild(
    _In_ PCSTR Mode,
    _In_ HANDLE Mapping,
    _In_ RECOVERY_SLOT Slot,
    _In_ DWORD PriorityClass)
{
    CHAR Path[MAX_PATH], CommandLine[MAX_PATH * 2];
    STARTUPINFOA StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;

    if (!GetModuleFileNameA(NULL, Path, ARRAYSIZE(Path)))
        return NULL;

    StringCbPrintfA(CommandLine, sizeof(CommandLine), "\"%s\" ApplicationRecovery child %s %lu %u",
                    Path, Mode, HandleToUlong(Mapping), Slot);
    if (!CreateProcessA(NULL, CommandLine, NULL, NULL, TRUE, PriorityClass, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ok(0, "CreateProcess(%s) failed: %lu\n", Mode, GetLastError());
        return NULL;
    }

    CloseHandle(ProcessInfo.hThread);
    return ProcessInfo.hProcess;
}

static
DWORD
WaitChild(
    _In_ PCSTR Mode,
    _In_opt_ HANDLE Process)
{
    DWORD Wait, ExitCode = 0xDEADBEEF;

    if (!Process)
        return ExitCode;

    Wait = WaitForSingleObject(Process, CHILD_WAIT_MS);
    ok(Wait == WAIT_OBJECT_0, "%s child did not exit (%lu)\n", Mode, Wait);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Process, 0xDEAD);
    else
        GetExitCodeProcess(Process, &ExitCode);

    CloseHandle(Process);
    return ExitCode;
}

static
VOID
TestLocalRegistration(VOID)
{
    APPLICATION_RECOVERY_CALLBACK Callback;
    PVOID Parameter;
    DWORD PingInterval, Flags;
    BOOL Cancelled = 7;
    HRESULT hr;

    hr = pApplicationRecoveryInProgress(&Cancelled);
    ok_hex(hr, E_FAIL);
    ok_int(Cancelled, 7);

    Callback = (APPLICATION_RECOVERY_CALLBACK)(ULONG_PTR)1;
    hr = pGetApplicationRecoveryCallback(GetCurrentProcess(), &Callback, NULL, NULL, NULL);
    ok_hex(hr, S_FALSE);

    hr = pRegisterApplicationRecoveryCallback(ReturnCallback, &Cancelled, 0, 0);
    ok_hex(hr, S_OK);

    Callback = NULL;
    Parameter = NULL;
    PingInterval = Flags = 0xDEADBEEF;
    hr = pGetApplicationRecoveryCallback(GetCurrentProcess(), &Callback, &Parameter, &PingInterval, &Flags);
    ok_hex(hr, S_OK);
    ok(Callback == ReturnCallback, "Callback %p, expected %p\n", Callback, ReturnCallback);
    ok_ptr(Parameter, &Cancelled);
    ok_long(PingInterval, RECOVERY_DEFAULT_PING_INTERVAL * 10000);
    ok_long(Flags, 0);

    hr = pRegisterApplicationRecoveryCallback(CrashCallback, NULL, RECOVERY_MAX_PING_INTERVAL, 0);
    ok_hex(hr, S_OK);
    hr = pGetApplicationRecoveryCallback(GetCurrentProcess(), &Callback, &Parameter, &PingInterval, &Flags);
    ok_hex(hr, S_OK);
    ok(Callback == CrashCallback, "Callback %p, expected %p\n", Callback, CrashCallback);
    ok_ptr(Parameter, NULL);
    ok(PingInterval == (DWORD)RECOVERY_MAX_PING_INTERVAL * 10000, "PingInterval %lu\n", PingInterval);

    hr = pRegisterApplicationRecoveryCallback(ReturnCallback, &Cancelled, RECOVERY_MAX_PING_INTERVAL + 1, 0);
    ok_hex(hr, E_INVALIDARG);
    hr = pGetApplicationRecoveryCallback(GetCurrentProcess(), &Callback, &Parameter, NULL, NULL);
    ok_hex(hr, S_OK);
    ok(Callback == CrashCallback, "Callback %p, expected %p\n", Callback, CrashCallback);

    hr = pRegisterApplicationRecoveryCallback(NULL, NULL, 0, 0);
    ok(FAILED(hr), "Register(NULL) returned 0x%lx\n", hr);

    hr = pGetApplicationRecoveryCallback(GetCurrentProcess(), NULL, &Parameter, NULL, NULL);
    ok_hex(hr, E_INVALIDARG);
    hr = pGetApplicationRecoveryCallback(NULL, &Callback, NULL, NULL, NULL);
    ok_hex(hr, E_INVALIDARG);

    hr = pUnregisterApplicationRecoveryCallback();
    ok_hex(hr, S_OK);
    hr = pGetApplicationRecoveryCallback(GetCurrentProcess(), &Callback, NULL, NULL, NULL);
    ok_hex(hr, S_FALSE);
    hr = pUnregisterApplicationRecoveryCallback();
    ok_hex(hr, S_OK);

    hr = pApplicationRecoveryInProgress(&Cancelled);
    ok_hex(hr, E_FAIL);
}

static
VOID
TestRemoteQuery(
    _In_ HANDLE Mapping)
{
    APPLICATION_RECOVERY_CALLBACK Callback;
    HANDLE Process, Limited;
    PVOID Parameter;
    DWORD PingInterval, Flags, Wait, WerFlags;
    HRESULT hr;

    RtlZeroMemory((PUCHAR)Shared + FIELD_OFFSET(RECOVERY_SHARED, RegisteredCallback),
                  sizeof(*Shared) - FIELD_OFFSET(RECOVERY_SHARED, RegisteredCallback));
    Process = StartChild("remote", Mapping, SlotRemote, 0);
    if (!Process)
        return;

    Wait = WaitForSingleObject(Shared->ReadyEvent, CHILD_WAIT_MS);
    ok(Wait == WAIT_OBJECT_0, "remote child not ready (%lu)\n", Wait);
    ok_hex(Shared->Register, S_OK);

    Callback = NULL;
    Parameter = NULL;
    PingInterval = Flags = 0xDEADBEEF;
    hr = pGetApplicationRecoveryCallback(Process, &Callback, &Parameter, &PingInterval, &Flags);
    ok_hex(hr, S_OK);
    ok((ULONG_PTR)Callback == Shared->RegisteredCallback, "Callback %p, expected %I64x\n", Callback, Shared->RegisteredCallback);
    ok((ULONG_PTR)Parameter == Shared->RegisteredParameter, "Parameter %p, expected %I64x\n", Parameter, Shared->RegisteredParameter);
    ok_long(PingInterval, 1234 * 10000);
    ok_long(Flags, 0);

    WerFlags = 0xDEADBEEF;
    hr = pWerGetFlags(Process, &WerFlags);
    ok_hex(hr, S_OK);
    ok_long(WerFlags, WER_FAULT_REPORTING_FLAG_NOHEAP | WER_FAULT_REPORTING_FLAG_QUEUE);

    Limited = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetProcessId(Process));
    ok(Limited != NULL, "OpenProcess failed: %lu\n", GetLastError());
    if (Limited)
    {
        hr = pGetApplicationRecoveryCallback(Limited, &Callback, NULL, NULL, NULL);
        ok(FAILED(hr), "GetApplicationRecoveryCallback without PROCESS_VM_READ returned 0x%lx\n", hr);
        hr = pWerGetFlags(Limited, &WerFlags);
        ok(FAILED(hr), "WerGetFlags without PROCESS_VM_READ returned 0x%lx\n", hr);
        CloseHandle(Limited);
    }

    SetEvent(Shared->DoneEvent);
    Wait = WaitForSingleObject(Process, CHILD_WAIT_MS);
    ok(Wait == WAIT_OBJECT_0, "remote child did not exit (%lu)\n", Wait);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Process, 0xDEAD);
    CloseHandle(Process);
    ResetEvent(Shared->DoneEvent);

    Process = StartChild("none", Mapping, SlotRemote, 0);
    if (!Process)
        return;

    Wait = WaitForSingleObject(Shared->ReadyEvent, CHILD_WAIT_MS);
    ok(Wait == WAIT_OBJECT_0, "child not ready (%lu)\n", Wait);
    hr = pGetApplicationRecoveryCallback(Process, &Callback, NULL, NULL, NULL);
    ok_hex(hr, S_FALSE);
    hr = pWerGetFlags(Process, &WerFlags);
    ok_hex(hr, HRESULT_FROM_WIN32(ERROR_NOT_FOUND));

    SetEvent(Shared->DoneEvent);
    Wait = WaitForSingleObject(Process, CHILD_WAIT_MS);
    ok(Wait == WAIT_OBJECT_0, "child did not exit (%lu)\n", Wait);
    if (Wait != WAIT_OBJECT_0)
        TerminateProcess(Process, 0xDEAD);
    CloseHandle(Process);
    ResetEvent(Shared->DoneEvent);
}

static
VOID
TestRecovery(
    _In_ HANDLE Mapping)
{
    static const struct
    {
        PCSTR Mode;
        RECOVERY_SLOT Slot;
        DWORD PriorityClass;
    } Children[] =
    {
        { "ping", SlotPing, ABOVE_NORMAL_PRIORITY_CLASS },
        { "hang", SlotHang, ABOVE_NORMAL_PRIORITY_CLASS },
        { "crash", SlotCrash, 0 },
        { "return", SlotReturn, 0 },
        { "unregistered", SlotUnregistered, 0 },
        { "finished", SlotFinished, 0 },
    };
    HANDLE Processes[RTL_NUMBER_OF(Children)];
    DWORD ExitCodes[SlotCount];
    PRECOVERY_SHARED Child;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Children); Index++)
    {
        RtlZeroMemory(&View[Children[Index].Slot], sizeof(View[Children[Index].Slot]));
        Processes[Index] = StartChild(Children[Index].Mode, Mapping, Children[Index].Slot, Children[Index].PriorityClass);
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Children); Index++)
        ExitCodes[Children[Index].Slot] = WaitChild(Children[Index].Mode, Processes[Index]);

    Child = &View[SlotCrash];
    ok_hex(ExitCodes[SlotCrash], STATUS_ACCESS_VIOLATION);
    ok_hex(Child->Register, S_OK);
    ok_long(Child->CallbackCalls, 1);
    ok(Child->Parameter == Child->RegisteredParameter, "Parameter %I64x, expected %I64x\n", Child->Parameter, Child->RegisteredParameter);
    ok_hex(Child->InProgress, S_OK);
    ok_int(Child->Cancelled, FALSE);
    ok_hex(Child->InProgressNull, E_INVALIDARG);
    ok_hex(Child->GetCallback, S_OK);
    ok(Child->GetCallbackAddress != 0, "No callback reported during recovery\n");
    ok_hex(Child->RegisterFile, WER_E_INVALID_STATE);
    ok_hex(Child->RegisterMemoryBlock, WER_E_INVALID_STATE);
    ok_hex(Child->RegisterRuntimeModule, WER_E_INVALID_STATE);
    ok_long(Child->AfterFinished, 0);

    Child = &View[SlotHang];
    ok_hex(ExitCodes[SlotHang], STATUS_ACCESS_VIOLATION);
    ok_long(Child->CallbackCalls, 1);
    ok_long(Child->AfterSleep, 0);

    Child = &View[SlotPing];
    ok_hex(ExitCodes[SlotPing], STATUS_ACCESS_VIOLATION);
    ok_long(Child->CallbackCalls, 1);
    ok_hex(Child->InProgress, S_OK);
    ok_int(Child->Cancelled, FALSE);
    ok_hex(Child->InProgressSecond, S_OK);
    ok_long(Child->Done, 1);
    ok_long(Child->AfterFinished, 0);

    ok_hex(ExitCodes[SlotReturn], STATUS_ACCESS_VIOLATION);
    ok_long(View[SlotReturn].CallbackCalls, 1);

    ok_hex(ExitCodes[SlotUnregistered], STATUS_ACCESS_VIOLATION);
    ok_long(View[SlotUnregistered].CallbackCalls, 0);

    ok_hex(ExitCodes[SlotFinished], CHILD_EXIT_RETURNED);
}

static
BOOL
LoadFunctions(VOID)
{
    HMODULE Kernel32 = GetModuleHandleW(L"kernel32.dll");

    pRegisterApplicationRecoveryCallback = (FN_RegisterApplicationRecoveryCallback)GetProcAddress(Kernel32, "RegisterApplicationRecoveryCallback");
    pUnregisterApplicationRecoveryCallback = (FN_UnregisterApplicationRecoveryCallback)GetProcAddress(Kernel32, "UnregisterApplicationRecoveryCallback");
    pGetApplicationRecoveryCallback = (FN_GetApplicationRecoveryCallback)GetProcAddress(Kernel32, "GetApplicationRecoveryCallback");
    pApplicationRecoveryInProgress = (FN_ApplicationRecoveryInProgress)GetProcAddress(Kernel32, "ApplicationRecoveryInProgress");
    pApplicationRecoveryFinished = (FN_ApplicationRecoveryFinished)GetProcAddress(Kernel32, "ApplicationRecoveryFinished");
    pWerRegisterFile = (FN_WerRegisterFile)GetProcAddress(Kernel32, "WerRegisterFile");
    pWerRegisterMemoryBlock = (FN_WerRegisterMemoryBlock)GetProcAddress(Kernel32, "WerRegisterMemoryBlock");
    pWerRegisterRuntimeExceptionModule = (FN_WerRegisterRuntimeExceptionModule)GetProcAddress(Kernel32, "WerRegisterRuntimeExceptionModule");
    pWerSetFlags = (FN_WerSetFlags)GetProcAddress(Kernel32, "WerSetFlags");
    pWerGetFlags = (FN_WerGetFlags)GetProcAddress(Kernel32, "WerGetFlags");

    return pRegisterApplicationRecoveryCallback && pUnregisterApplicationRecoveryCallback &&
           pGetApplicationRecoveryCallback && pApplicationRecoveryInProgress && pApplicationRecoveryFinished &&
           pWerRegisterFile && pWerRegisterMemoryBlock && pWerRegisterRuntimeExceptionModule &&
           pWerSetFlags && pWerGetFlags;
}

START_TEST(ApplicationRecovery)
{
    SECURITY_ATTRIBUTES Attributes = { sizeof(Attributes), NULL, TRUE };
    HANDLE Mapping;
    char **Arguments;
    int ArgumentCount;
    ULONG Slot;

    if (!LoadFunctions())
    {
        skip("Application recovery exports are missing\n");
        return;
    }

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 6 && !strcmp(Arguments[2], "child"))
    {
        Mapping = ULongToHandle(strtoul(Arguments[4], NULL, 10));
        Slot = strtoul(Arguments[5], NULL, 10);
        View = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, SlotCount * sizeof(*View));
        if (!View || Slot >= SlotCount)
            ExitProcess(0xBAD);
        Shared = &View[Slot];
        RunChildMode(Arguments[3]);
        return;
    }

    Mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &Attributes, PAGE_READWRITE, 0, SlotCount * sizeof(*View), NULL);
    ok(Mapping != NULL, "CreateFileMapping failed: %lu\n", GetLastError());
    if (!Mapping)
        return;

    View = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, SlotCount * sizeof(*View));
    ok(View != NULL, "MapViewOfFile failed: %lu\n", GetLastError());
    if (!View)
    {
        CloseHandle(Mapping);
        return;
    }
    Shared = &View[SlotRemote];

    Shared->ReadyEvent = CreateEventW(&Attributes, FALSE, FALSE, NULL);
    Shared->DoneEvent = CreateEventW(&Attributes, TRUE, FALSE, NULL);
    ok(Shared->ReadyEvent && Shared->DoneEvent, "CreateEvent failed: %lu\n", GetLastError());

    TestLocalRegistration();
    if (Shared->ReadyEvent && Shared->DoneEvent)
        TestRemoteQuery(Mapping);
    TestRecovery(Mapping);

    CloseHandle(Shared->ReadyEvent);
    CloseHandle(Shared->DoneEvent);
    UnmapViewOfFile(View);
    CloseHandle(Mapping);
}
