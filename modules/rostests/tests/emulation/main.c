/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Console program exercising a process that may run under an emulation host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ndk/rtlfuncs.h>
#include <pseh/pseh2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emulation_test.h"

#define WORKER_COUNT      8
#define WORKER_ITERATIONS 64
#define TEST_EXCEPTION    0xE0454D55

LONG EmuTestChecks;
LONG EmuTestFailures;

static CRITICAL_SECTION Lock;
static HANDLE Gate;
static DWORD TlsIndex;
static LONG Counter;
static LONG FaultsContinued;
static PVOID FaultPage;
static __declspec(thread) LONG StaticTls = 7;

static
DECLSPEC_NOINLINE
ULONG
ProbeStack(
    _In_ ULONG Depth)
{
    volatile UCHAR Page[8192];
    ULONG Sum;

    Page[0] = (UCHAR)Depth;
    Page[4096] = (UCHAR)(Depth + 1);
    Page[8191] = (UCHAR)(Depth + 2);
    Sum = Depth ? ProbeStack(Depth - 1) : 0;
    return Sum + Page[0] + Page[4096] + Page[8191];
}

static
DWORD
WINAPI
Worker(
    _In_ PVOID Parameter)
{
    ULONG_PTR Index = (ULONG_PTR)Parameter;
    ULONG Iteration;

    CHECK(StaticTls == 7);
    CHECK(ProbeStack(16) == 459);
    StaticTls = 100 + (LONG)Index;
    CHECK(TlsSetValue(TlsIndex, Parameter));
    CHECK(WaitForSingleObject(Gate, 30000) == WAIT_OBJECT_0);

    for (Iteration = 0; Iteration < WORKER_ITERATIONS; Iteration++)
    {
        SetLastError(0x123000 + (DWORD)Index);
        EnterCriticalSection(&Lock);
        Counter++;
        LeaveCriticalSection(&Lock);
        Sleep(1);
        CHECK(GetLastError() == 0x123000 + Index);
        CHECK(StaticTls == 100 + (LONG)Index);
        CHECK(TlsGetValue(TlsIndex) == Parameter);
    }

    return 0x40 + (DWORD)Index;
}

static
VOID
TestThreads(VOID)
{
    HANDLE Threads[WORKER_COUNT];
    DWORD Index, ExitCode;

    InitializeCriticalSection(&Lock);
    Gate = CreateEventW(NULL, TRUE, FALSE, NULL);
    TlsIndex = TlsAlloc();
    CHECK(Gate != NULL && TlsIndex != TLS_OUT_OF_INDEXES);
    StaticTls = 99;

    for (Index = 0; Index < WORKER_COUNT; Index++)
    {
        Threads[Index] = CreateThread(NULL, 0, Worker, (PVOID)(ULONG_PTR)(Index + 1), CREATE_SUSPENDED, NULL);
        CHECK(Threads[Index] != NULL);
        if (Threads[Index]) CHECK(ResumeThread(Threads[Index]) == 1);
    }

    CHECK(SetEvent(Gate));
    for (Index = 0; Index < WORKER_COUNT; Index++)
    {
        if (!Threads[Index]) continue;
        CHECK(WaitForSingleObject(Threads[Index], 60000) == WAIT_OBJECT_0);
        ExitCode = 0;
        CHECK(GetExitCodeThread(Threads[Index], &ExitCode));
        CHECK(ExitCode == 0x41 + Index);
        CloseHandle(Threads[Index]);
    }

    CHECK(Counter == WORKER_COUNT * WORKER_ITERATIONS);
    CHECK(StaticTls == 99);
    CHECK(TlsFree(TlsIndex));
    CloseHandle(Gate);
    DeleteCriticalSection(&Lock);
    printf("EMUTEST: %u threads finished, counter=%ld, TLS intact\n", WORKER_COUNT, Counter);
}

static
VOID
TestFiles(VOID)
{
    static const CHAR Message[] = "emulated guest file round trip";
    WCHAR Directory[MAX_PATH], Name[MAX_PATH];
    CHAR Buffer[sizeof(Message)];
    HANDLE File;
    DWORD Count;

    CHECK(GetTempPathW(RTL_NUMBER_OF(Directory), Directory) != 0);
    CHECK(GetTempFileNameW(Directory, L"emu", 0, Name) != 0);
    File = CreateFileW(Name,
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_TEMPORARY,
                       NULL);
    CHECK(File != INVALID_HANDLE_VALUE);
    if (File == INVALID_HANDLE_VALUE) return;

    Count = 0;
    CHECK(WriteFile(File, Message, sizeof(Message), &Count, NULL) && Count == sizeof(Message));
    CHECK(GetFileSize(File, NULL) == sizeof(Message));
    CHECK(SetFilePointer(File, 0, NULL, FILE_BEGIN) == 0);
    Count = 0;
    RtlZeroMemory(Buffer, sizeof(Buffer));
    CHECK(ReadFile(File, Buffer, sizeof(Buffer), &Count, NULL) && Count == sizeof(Message));
    CHECK(!memcmp(Message, Buffer, sizeof(Message)));
    CloseHandle(File);
    CHECK(GetFileAttributesW(Name) != INVALID_FILE_ATTRIBUTES);
    CHECK(DeleteFileW(Name));
    CHECK(GetFileAttributesW(Name) == INVALID_FILE_ATTRIBUTES);
    printf("EMUTEST: file round trip of %lu bytes: \"%s\"\n", Count, Buffer);
}

static
LONG
ExceptionFilter(
    _In_ PEXCEPTION_POINTERS Pointers,
    _Out_ PULONG_PTR Parameter)
{
    *Parameter = 0;
    if (Pointers->ExceptionRecord->ExceptionCode != TEST_EXCEPTION) return EXCEPTION_CONTINUE_SEARCH;
    if (Pointers->ExceptionRecord->NumberParameters == 2) *Parameter = Pointers->ExceptionRecord->ExceptionInformation[1];
    return EXCEPTION_EXECUTE_HANDLER;
}

static
VOID
TestStructuredExceptions(VOID)
{
    ULONG_PTR Arguments[2] = { 1, 0x5EC0DE };
    volatile LONG Caught = 0, Unwound = 0;
    ULONG_PTR Parameter = 0;
    DWORD OldProtect;
    PVOID Page;

    Page = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(Page != NULL);
    if (Page)
    {
        CHECK(VirtualProtect(Page, 4096, PAGE_READONLY, &OldProtect));
        _SEH2_TRY
        {
            *(volatile LONG *)Page = 1;
        }
        _SEH2_EXCEPT(_SEH2_GetExceptionCode() == STATUS_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            Caught = 1;
        }
        _SEH2_END;
        CHECK(Caught == 1 && *(LONG *)Page == 0);
        CHECK(VirtualFree(Page, 0, MEM_RELEASE));
    }

    Caught = 0;
    _SEH2_TRY
    {
        _SEH2_TRY
        {
            RaiseException(TEST_EXCEPTION, 0, RTL_NUMBER_OF(Arguments), Arguments);
        }
        _SEH2_FINALLY
        {
            Unwound = 1;
        }
        _SEH2_END;
    }
    _SEH2_EXCEPT(ExceptionFilter(_SEH2_GetExceptionInformation(), &Parameter))
    {
        Caught = 1;
    }
    _SEH2_END;
    CHECK(Caught == 1 && Unwound == 1 && Parameter == 0x5EC0DE);
    printf("EMUTEST: SEH caught an access violation and exception %08lx\n", (ULONG)TEST_EXCEPTION);
}

static
LONG
NTAPI
ContinueHandler(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    volatile double Scratch[16];
    UCHAR Buffer[512];
    DWORD OldProtect;
    ULONG Index;

    if (Pointers->ExceptionRecord->ExceptionCode != STATUS_ACCESS_VIOLATION ||
        Pointers->ExceptionRecord->NumberParameters < 2 ||
        (PVOID)Pointers->ExceptionRecord->ExceptionInformation[1] != FaultPage)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Scratch); Index++) Scratch[Index] = -1.0 / (Index + 1);
    memset(Buffer, 0xA5, sizeof(Buffer));
    if (Scratch[3] == 0.0 || Buffer[17] != 0xA5) return EXCEPTION_CONTINUE_SEARCH;
    if (!VirtualProtect(FaultPage, 4096, PAGE_READWRITE, &OldProtect)) return EXCEPTION_CONTINUE_SEARCH;
    InterlockedIncrement(&FaultsContinued);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static
DECLSPEC_NOINLINE
double
SumAcrossStore(
    _Out_ volatile LONG *Target,
    _In_ const volatile double *Seed)
{
    double V0 = Seed[0] * 1.25, V1 = Seed[1] * 2.5, V2 = Seed[2] * 3.75, V3 = Seed[3] * 5.0;
    double V4 = Seed[4] * 6.25, V5 = Seed[5] * 7.5, V6 = Seed[6] * 8.75, V7 = Seed[7] * 10.0;
    double V8 = Seed[8] * 11.25, V9 = Seed[9] * 12.5, V10 = Seed[10] * 13.75, V11 = Seed[11] * 15.0;

    *Target = 1;
    return V0 + V1 * 2 + V2 * 3 + V3 * 4 + V4 * 5 + V5 * 6 + V6 * 7 + V7 * 8 + V8 * 9 + V9 * 10 + V10 * 11 + V11 * 12;
}

static
VOID
TestFloatingStateAcrossFault(VOID)
{
    volatile double Seed[12];
    volatile LONG Plain = 0;
    double Expected, Actual;
    DWORD OldProtect;
    PVOID Handler;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Seed); Index++) Seed[Index] = 1.0 + Index / 7.0;

    FaultPage = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(FaultPage != NULL);
    if (!FaultPage) return;

    Handler = AddVectoredExceptionHandler(1, ContinueHandler);
    CHECK(Handler != NULL);
    CHECK(VirtualProtect(FaultPage, 4096, PAGE_READONLY, &OldProtect));

    Expected = SumAcrossStore(&Plain, Seed);
    Actual = SumAcrossStore((volatile LONG *)FaultPage, Seed);
    CHECK(FaultsContinued == 1);
    CHECK(*(LONG *)FaultPage == 1 && Plain == 1);
    CHECK(Actual == Expected);

    if (Handler) RemoveVectoredExceptionHandler(Handler);
    CHECK(VirtualFree(FaultPage, 0, MEM_RELEASE));
    printf("EMUTEST: floating-point registers survived a continued fault (%ld continued)\n", FaultsContinued);
}

int
main(
    int ArgumentCount,
    char **Arguments)
{
    PIMAGE_NT_HEADERS Headers = RtlImageNtHeader(GetModuleHandleW(NULL));
    SYSTEM_INFO System;

    GetSystemInfo(&System);
    printf("EMUTEST: hello from a machine %04x image, %u-bit pointers, processor architecture %u\n",
           Headers->FileHeader.Machine,
           (unsigned)(8 * sizeof(PVOID)),
           System.wProcessorArchitecture);
    CHECK(System.dwNumberOfProcessors > 0 && System.dwPageSize == 4096);
    CHECK(Headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
          System.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64);
    CHECK(ProbeStack(16) == 459);

    TestThreads();
    TestFiles();
    TestStructuredExceptions();
    TestFloatingStateAcrossFault();
    EmuTestCxxExceptions();

    printf("EMUTEST: END checks=%ld failures=%ld\n", EmuTestChecks, EmuTestFailures);
    DbgPrint("EMUTEST: END checks=%ld failures=%ld\n", EmuTestChecks, EmuTestFailures);
    if (EmuTestFailures) return 1;
    if (ArgumentCount == 3 && !strcmp(Arguments[1], "exit")) ExitProcess(strtoul(Arguments[2], NULL, 0));
    return 0;
}
