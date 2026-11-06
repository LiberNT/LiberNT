/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process exit ends the other threads before DLL_PROCESS_DETACH
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define EXIT_CODE 0x55
#define EXIT_SETTLE_MS 300
#define EXIT_LIMIT_MS 10000

typedef VOID (NTAPI *PRTL_EXIT_USER_PROCESS)(NTSTATUS);

typedef struct _EXIT_RESULT
{
    LONG Detached;
    DWORD SpinState;
    DWORD SleepState;
    LONG SpinAdvanced;
} EXIT_RESULT, *PEXIT_RESULT;

static volatile LONG SpinCounter;
static HANDLE SpinThread, SleepThread;
static PEXIT_RESULT Result;

static DWORD WINAPI
SpinInUserMode(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    for (;;)
        InterlockedIncrement(&SpinCounter);
    return 0;
}

static DWORD WINAPI
SleepInKernel(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    Sleep(INFINITE);
    return 0;
}

static VOID NTAPI
ExitTlsCallback(PVOID Module, DWORD Reason, PVOID Reserved)
{
    LONG Before;

    UNREFERENCED_PARAMETER(Module);
    UNREFERENCED_PARAMETER(Reserved);
    if (Reason != DLL_PROCESS_DETACH || Result == NULL || SpinThread == NULL)
        return;
    Result->SpinState = WaitForSingleObject(SpinThread, 0);
    Result->SleepState = WaitForSingleObject(SleepThread, 0);
    Before = SpinCounter;
    Sleep(50);
    Result->SpinAdvanced = (SpinCounter != Before);
    InterlockedExchange(&Result->Detached, 1);
}

#if defined(_MSC_VER)
#pragma section(".CRT$XLB", long, read)
__declspec(allocate(".CRT$XLB")) PIMAGE_TLS_CALLBACK ExitTlsCallbackEntry = ExitTlsCallback;
#else
PIMAGE_TLS_CALLBACK ExitTlsCallbackEntry __attribute__((section(".CRT$XLB"), used)) = ExitTlsCallback;
#endif

static void
RunChild(const char *MappingName, const char *Mode)
{
    PRTL_EXIT_USER_PROCESS ExitUserProcess =
        (PRTL_EXIT_USER_PROCESS)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlExitUserProcess");
    HANDLE Mapping = OpenFileMappingA(FILE_MAP_WRITE, FALSE, MappingName);

    if (Mapping == NULL)
        ExitProcess(2);
    Result = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*Result));
    if (Result == NULL || ExitUserProcess == NULL)
        ExitProcess(3);
    SpinThread = CreateThread(NULL, 0, SpinInUserMode, NULL, 0, NULL);
    SleepThread = CreateThread(NULL, 0, SleepInKernel, NULL, 0, NULL);
    if (SpinThread == NULL || SleepThread == NULL)
        ExitProcess(4);
    Sleep(EXIT_SETTLE_MS);
    if (strcmp(Mode, "rtl") == 0)
        ExitUserProcess(EXIT_CODE);
    ExitProcess(EXIT_CODE);
}

static void
TestExit(const char *Mode)
{
    PROCESS_INFORMATION Info = {0};
    STARTUPINFOA Startup = {0};
    char Name[64], Command[MAX_PATH + 128];
    char **Argv;
    HANDLE Mapping;
    PEXIT_RESULT Shared;
    DWORD Wait, Code = 0xdeadbeef;

    sprintf(Name, "Local\\RtlExitUserProcess_%lu_%s", GetCurrentProcessId(), Mode);
    Mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(EXIT_RESULT), Name);
    ok(Mapping != NULL, "%s: CreateFileMapping failed: %lu\n", Mode, GetLastError());
    if (Mapping == NULL)
        return;
    Shared = MapViewOfFile(Mapping, FILE_MAP_WRITE, 0, 0, sizeof(*Shared));
    ok(Shared != NULL, "%s: MapViewOfFile failed: %lu\n", Mode, GetLastError());
    if (Shared == NULL)
    {
        CloseHandle(Mapping);
        return;
    }
    Shared->Detached = 0;
    Shared->SpinState = 0xdeadbeef;
    Shared->SleepState = 0xdeadbeef;
    Shared->SpinAdvanced = -1;
    winetest_get_mainargs(&Argv);
    sprintf(Command, "\"%s\" RtlExitUserProcess child %s %s", Argv[0], Name, Mode);
    Startup.cb = sizeof(Startup);
    ok(CreateProcessA(NULL, Command, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Info),
       "%s: CreateProcess failed: %lu\n", Mode, GetLastError());
    if (Info.hProcess != NULL)
    {
        Wait = WaitForSingleObject(Info.hProcess, EXIT_LIMIT_MS);
        ok(Wait == WAIT_OBJECT_0, "%s: child did not exit: %lu\n", Mode, Wait);
        if (Wait != WAIT_OBJECT_0)
            TerminateProcess(Info.hProcess, 1);
        GetExitCodeProcess(Info.hProcess, &Code);
        ok(Code == EXIT_CODE, "%s: exit code %#lx\n", Mode, Code);
        ok(Shared->Detached == 1, "%s: DLL_PROCESS_DETACH did not reach the executable\n", Mode);
        ok(Shared->SpinState == WAIT_OBJECT_0,
           "%s: a thread running in user mode was alive during DLL_PROCESS_DETACH: %lu\n", Mode, Shared->SpinState);
        ok(Shared->SleepState == WAIT_OBJECT_0,
           "%s: a thread waiting in the kernel was alive during DLL_PROCESS_DETACH: %lu\n", Mode, Shared->SleepState);
        ok(Shared->SpinAdvanced == 0, "%s: a thread kept running during DLL_PROCESS_DETACH: %ld\n",
           Mode, Shared->SpinAdvanced);
        CloseHandle(Info.hThread);
        CloseHandle(Info.hProcess);
    }
    UnmapViewOfFile(Shared);
    CloseHandle(Mapping);
}

START_TEST(RtlExitUserProcess)
{
    char **Argv;
    int Argc = winetest_get_mainargs(&Argv);

    if (Argc >= 5 && strcmp(Argv[2], "child") == 0)
    {
        RunChild(Argv[3], Argv[4]);
        return;
    }
    TestExit("rtl");
    TestExit("kernel32");
}
