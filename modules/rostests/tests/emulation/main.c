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
    printf("EMUTEST: %d threads finished, counter=%ld, TLS intact\n", WORKER_COUNT, Counter);
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
    _Out_ volatile ULONG_PTR *Parameter)
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
    volatile ULONG_PTR Parameter = 0;
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

static LONG UserApcs;
static ULONG_PTR UserApcData;
static LONG IoCompletions;
static DWORD IoCompletionBytes;
static LONG TimerApcs;

static
VOID
CALLBACK
UserApc(
    _In_ ULONG_PTR Data)
{
    UserApcData = Data;
    InterlockedIncrement(&UserApcs);
}

static
VOID
CALLBACK
IoCompletion(
    _In_ DWORD ErrorCode,
    _In_ DWORD Bytes,
    _In_ LPOVERLAPPED Overlapped)
{
    CHECK(ErrorCode == ERROR_SUCCESS);
    IoCompletionBytes = Bytes;
    InterlockedIncrement(&IoCompletions);
}

static
VOID
CALLBACK
TimerCompletion(
    _In_opt_ PVOID Context,
    _In_ DWORD Low,
    _In_ DWORD High)
{
    CHECK(Context == (PVOID)(ULONG_PTR)0x7135);
    InterlockedIncrement(&TimerApcs);
}

static
VOID
TestApcs(VOID)
{
    static const CHAR Message[] = "asynchronous procedure call";
    WCHAR Directory[MAX_PATH], Name[MAX_PATH];
    OVERLAPPED Overlapped = { 0 };
    LARGE_INTEGER Due;
    HANDLE File, Timer;
    ULONG Index;

    CHECK(QueueUserAPC(UserApc, GetCurrentThread(), 0x5A5A) != 0);
    CHECK(UserApcs == 0);
    CHECK(SleepEx(0, TRUE) == WAIT_IO_COMPLETION);
    CHECK(UserApcs == 1 && UserApcData == 0x5A5A);
    CHECK(SleepEx(0, TRUE) == 0);

    CHECK(GetTempPathW(RTL_NUMBER_OF(Directory), Directory) != 0);
    CHECK(GetTempFileNameW(Directory, L"apc", 0, Name) != 0);
    File = CreateFileW(Name, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_FLAG_OVERLAPPED | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    CHECK(File != INVALID_HANDLE_VALUE);
    if (File != INVALID_HANDLE_VALUE)
    {
        CHECK(WriteFileEx(File, Message, sizeof(Message), &Overlapped, IoCompletion));
        for (Index = 0; Index < 100 && !IoCompletions; Index++) SleepEx(10, TRUE);
        CHECK(IoCompletions == 1 && IoCompletionBytes == sizeof(Message));
        CloseHandle(File);
    }

    Timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    CHECK(Timer != NULL);
    if (Timer)
    {
        Due.QuadPart = -10 * 1000 * 10;
        CHECK(SetWaitableTimer(Timer, &Due, 0, TimerCompletion, (PVOID)(ULONG_PTR)0x7135, FALSE));
        for (Index = 0; Index < 100 && !TimerApcs; Index++) SleepEx(10, TRUE);
        CHECK(TimerApcs == 1);
        CloseHandle(Timer);
    }

    printf("EMUTEST: %ld user, %ld I/O and %ld timer APCs delivered\n", UserApcs, IoCompletions, TimerApcs);
}

#if defined(_M_AMD64) || defined(__x86_64__)
#define CONTEXT_PC(Context) ((Context)->Rip)
#define CONTEXT_SP(Context) ((Context)->Rsp)
#define CALL_RETURN_SLOT    sizeof(PVOID)
#elif defined(_M_IX86) || defined(__i386__)
#define CONTEXT_PC(Context) ((Context)->Eip)
#define CONTEXT_SP(Context) ((Context)->Esp)
#define CALL_RETURN_SLOT    sizeof(PVOID)
#else
#define CONTEXT_PC(Context) ((Context)->Pc)
#define CONTEXT_SP(Context) ((Context)->Sp)
#define CALL_RETURN_SLOT    0
#endif

static volatile LONG SpinStop;
static volatile LONG SpinRounds;
static volatile ULONG_PTR SpinMarker;
static volatile LONG WaiterStarted;

static
DWORD
WINAPI
Spinner(
    _In_ PVOID Parameter)
{
    volatile UCHAR Marker = 0;
    double Sum = 0.0;
    LONG Rounds = 0;

    SpinMarker = (ULONG_PTR)&Marker;
    while (!SpinStop)
    {
        Sum += 1.0;
        Rounds++;
        SpinRounds = Rounds;
    }
    return Sum == (double)Rounds ? 0 : 1;
}

static
DECLSPEC_NOINLINE
VOID
WINAPI
Redirected(VOID)
{
    ExitThread(0x42);
}

static
DWORD
WINAPI
Waiter(
    _In_ PVOID Parameter)
{
    InterlockedExchange(&WaiterStarted, 1);
    return WaitForSingleObject((HANDLE)Parameter, INFINITE) == WAIT_OBJECT_0 ? 0x33 : 1;
}

static
BOOLEAN
InModule(
    _In_ ULONG_PTR Address,
    _In_opt_ HMODULE Module)
{
    PIMAGE_NT_HEADERS Headers = Module ? RtlImageNtHeader(Module) : NULL;

    return Headers && Address >= (ULONG_PTR)Module &&
           Address < (ULONG_PTR)Module + Headers->OptionalHeader.SizeOfImage;
}

static
HANDLE
StartSpinner(VOID)
{
    HANDLE Thread;
    ULONG Index;

    SpinStop = 0;
    SpinRounds = 0;
    Thread = CreateThread(NULL, 0, Spinner, NULL, 0, NULL);
    CHECK(Thread != NULL);
    for (Index = 0; Thread && Index < 5000 && !SpinRounds; Index++) Sleep(1);
    CHECK(SpinRounds != 0);
    return Thread;
}

static
VOID
TestThreadContext(VOID)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    CONTEXT Context, Again;
    HANDLE Thread, Event;
    BOOLEAN Waiting;
    DWORD ExitCode;
    LONG Before;
    ULONG Index;

    Thread = StartSpinner();
    if (!Thread) return;
    for (Index = 0; Index < 16; Index++)
    {
        CHECK(SuspendThread(Thread) == 0);
        Before = SpinRounds;
        Sleep(5);
        CHECK(SpinRounds == Before);
        Context.ContextFlags = CONTEXT_FULL;
        CHECK(GetThreadContext(Thread, &Context));
        CHECK(InModule(CONTEXT_PC(&Context), GetModuleHandleW(NULL)));
        CHECK(CONTEXT_SP(&Context) <= SpinMarker && SpinMarker - CONTEXT_SP(&Context) < 0x1000);
        CHECK(ResumeThread(Thread) == 1);
        Sleep(1);
    }

    CHECK(SuspendThread(Thread) == 0);
    CHECK(SuspendThread(Thread) == 1);
    Context.ContextFlags = CONTEXT_FULL;
    Again.ContextFlags = CONTEXT_FULL;
    CHECK(GetThreadContext(Thread, &Context));
    CHECK(GetThreadContext(Thread, &Again));
    CHECK(CONTEXT_PC(&Context) == CONTEXT_PC(&Again) && CONTEXT_SP(&Context) == CONTEXT_SP(&Again));
    CHECK(ResumeThread(Thread) == 2);
    CONTEXT_PC(&Context) = (ULONG_PTR)Redirected;
    CONTEXT_SP(&Context) = ((CONTEXT_SP(&Context) - 0x100) & ~(ULONG_PTR)15) - CALL_RETURN_SLOT;
    CHECK(SetThreadContext(Thread, &Context));
    CHECK(ResumeThread(Thread) == 1);
    CHECK(WaitForSingleObject(Thread, 30000) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(Thread, &ExitCode) && ExitCode == 0x42);
    CloseHandle(Thread);

    Thread = StartSpinner();
    if (!Thread) return;
    for (Index = 0; Index < 32; Index++)
    {
        CHECK(SuspendThread(Thread) == 0);
        Context.ContextFlags = CONTEXT_FULL | CONTEXT_FLOATING_POINT;
        CHECK(GetThreadContext(Thread, &Context));
        CHECK(ResumeThread(Thread) == 1);
        Sleep(1);
    }
    SpinStop = 1;
    CHECK(WaitForSingleObject(Thread, 30000) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(Thread, &ExitCode) && ExitCode == 0);
    CloseHandle(Thread);

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(Event != NULL);
    if (!Event) return;
    WaiterStarted = 0;
    Thread = CreateThread(NULL, 0, Waiter, Event, 0, NULL);
    CHECK(Thread != NULL);
    if (Thread)
    {
        for (Index = 0; Index < 5000 && !WaiterStarted; Index++) Sleep(1);
        CHECK(WaiterStarted != 0);
        Waiting = FALSE;
        for (Index = 0; Index < 500; Index++)
        {
            Sleep(10);
            CHECK(SuspendThread(Thread) == 0);
            Context.ContextFlags = CONTEXT_FULL;
            CHECK(GetThreadContext(Thread, &Context));
            Waiting = InModule(CONTEXT_PC(&Context), Ntdll);
            if (Waiting || sizeof(PVOID) != 8) break;
            CHECK(ResumeThread(Thread) == 1);
        }
        if (sizeof(PVOID) == 8) CHECK(Waiting);
        CHECK(ResumeThread(Thread) == 1);
        CHECK(SetEvent(Event));
        CHECK(WaitForSingleObject(Thread, 30000) == WAIT_OBJECT_0);
        CHECK(GetExitCodeThread(Thread, &ExitCode) && ExitCode == 0x33);
        CloseHandle(Thread);
    }
    CloseHandle(Event);
    printf("EMUTEST: suspended thread contexts read, redirected and resumed\n");
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

typedef int (*PINDIRECT_INT)(int, short, float, double);
typedef double (*PINDIRECT_DOUBLE)(float, double, int);

static
int
IndirectInt(
    _In_ int A,
    _In_ short B,
    _In_ float C,
    _In_ double D)
{
    return A * B - (int)(C * 2) - (int)D;
}

static
double
IndirectDouble(
    _In_ float A,
    _In_ double B,
    _In_ int C)
{
    return A * B + C;
}

static
VOID
TestIndirectCalls(VOID)
{
    static PINDIRECT_INT volatile IntTarget = IndirectInt;
    static PINDIRECT_DOUBLE volatile DoubleTarget = IndirectDouble;

    CHECK(IntTarget(-6, 7, 1.5f, 4.0) == -49);
    CHECK(DoubleTarget(0.5f, -3.0, 9) == 7.5);
}

#if defined(_M_AMD64) || defined(__x86_64__)
#include <emmintrin.h>
#endif

static
VOID
TestMisalignedVectors(VOID)
{
    DECLSPEC_ALIGN(16) UCHAR Source[64];
    DECLSPEC_ALIGN(16) UCHAR Target[64];
    ULONG Offset, Index;
    LONG Mismatches = 0;

    for (Index = 0; Index < sizeof(Source); Index++) Source[Index] = (UCHAR)(Index * 7 + 1);
    for (Offset = 1; Offset < 16; Offset++)
    {
        RtlZeroMemory(Target, sizeof(Target));
#if defined(_M_AMD64) || defined(__x86_64__)
        _mm_storeu_si128((__m128i *)(Target + Offset), _mm_loadu_si128((const __m128i *)(Source + Offset)));
        _mm_storel_epi64((__m128i *)(Target + Offset + 16), _mm_loadl_epi64((const __m128i *)(Source + Offset + 16)));
        _mm_store_ss((float *)(Target + Offset + 24), _mm_load_ss((const float *)(Source + Offset + 24)));
#else
        memcpy(Target + Offset, Source + Offset, 28);
#endif
        for (Index = 0; Index < sizeof(Target); Index++)
        {
            if (Target[Index] != ((Index >= Offset && Index < Offset + 28) ? Source[Index] : 0)) Mismatches++;
        }
    }
    CHECK(Mismatches == 0);
    printf("EMUTEST: misaligned 128-bit, 64-bit and 32-bit vector moves verified\n");
}

#if defined(_M_AMD64) || defined(__x86_64__)
#include <reactos/emuhost.h>
#include "rv64xtest.h"

#define RV64X_STRING2(Value) #Value
#define RV64X_STRING(Value) RV64X_STRING2(Value)

__attribute__((naked))
static
ULONG64
Rv64xLoadHost(
    _In_ PCWSTR Name)
{
    __asm__ __volatile__("movq %rcx, %r10\n\t"
                         "movl $" RV64X_STRING(EMU_HOST_SERVICE_LOAD) ", %eax\n\t"
                         "syscall\n\t"
                         "ret");
}

static
int
Rv64xIntCallback(
    _In_ int A,
    _In_ short B,
    _In_ unsigned char C,
    _In_ float D,
    _In_ double E,
    _In_ int F,
    _In_ float G,
    _In_ long long H,
    _In_ int I,
    _In_ double J)
{
    CHECK(A == -7 && B == -2 && C == 250 && D == 1.5f && E == -2.25 && F == 100000 && G == 0.5f &&
          H == -5000000000LL && I == 3 && J == 9.0);
    return -5;
}

static
float
Rv64xFloatCallback(
    _In_ float A,
    _In_ double B)
{
    CHECK(A == 1.25f && B == 2.5);
    return A + (float)B;
}

static
VOID
TestRv64X(VOID)
{
    WCHAR Directory[MAX_PATH];
    PRV64X_TEST_TABLE Table;
    char Buffer[64] = { 0 };
    UINT Length;

    Length = GetWindowsDirectoryW(Directory, MAX_PATH - 16);
    if (!Length || Length >= MAX_PATH - 16) return;
    wcscat(Directory, L"\\SystemAMD64");
    if (GetFileAttributesW(Directory) == INVALID_FILE_ATTRIBUTES) return;

    Table = (PRV64X_TEST_TABLE)(ULONG_PTR)Rv64xLoadHost(RV64X_TEST_LIBRARY);
    if (!Table)
    {
        printf("EMUTEST: no RV64X host library\n");
        return;
    }
    CHECK(Table->Size >= sizeof(*Table));
    CHECK(((int (*)(int, short, unsigned char, long long))(ULONG_PTR)Table->Add)(-5, -300, 200, 10000000000LL) ==
          (int)(-5 - 300 + 200 + 10000000000LL));
    CHECK(((double (*)(float, double, int, float, double, int, float, double, signed char, unsigned short))
           (ULONG_PTR)Table->Mix)(1.5f, 2.25, -3, 4.5f, -5.25, 6, 7.5f, 8.25, -9, 65000) ==
          1.5f + 2.25 * 2 + -3 * 4 + 4.5f * 8 + -5.25 * 16 + 6 * 32 + 7.5f * 64 + 8.25 * 128 + -9 * 256 + 65000 * 512.0);
    CHECK(((int (*)(PRV64X_INT_CALLBACK))(ULONG_PTR)Table->CallInt)(Rv64xIntCallback) == -15);
    CHECK(((float (*)(PRV64X_FLOAT_CALLBACK))(ULONG_PTR)Table->CallFloat)(Rv64xFloatCallback) == 7.5f);
    CHECK(((int (*)(PRV64X_FORMAT_CALLBACK, char *, size_t))(ULONG_PTR)Table->CallFormat)(
              (PRV64X_FORMAT_CALLBACK)_snprintf, Buffer, sizeof(Buffer)) == 13);
    CHECK(!strcmp(Buffer, "42 3.25 x 0.5"));
    CHECK(((int (*)(int, short, unsigned char, long long))(ULONG_PTR)Table->NativeAdd)(7, -2, 255, -1LL) == 259);
    CHECK(((double (*)(float, double, int, float, double, int, float, double, signed char, unsigned short))
           (ULONG_PTR)Table->NativeMix)(0.5f, 1.0, 2, 0.25f, -1.0, 1, 2.0f, 0.5, 1, 2) ==
          0.5f + 1.0 * 2 + 2 * 4 + 0.25f * 8 + -1.0 * 16 + 1 * 32 + 2.0f * 64 + 0.5 * 128 + 1 * 256 + 2 * 512.0);
    printf("EMUTEST: RV64X entry descriptors, exit thunks and direct native entry verified\n");
}
#else
static
VOID
TestRv64X(VOID)
{
}
#endif

int
main(
    int ArgumentCount,
    char **Arguments)
{
    PIMAGE_NT_HEADERS Headers = RtlImageNtHeader(GetModuleHandleW(NULL));
    SYSTEM_INFO System;

    setvbuf(stdout, NULL, _IONBF, 0);
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
    TestApcs();
    TestThreadContext();
    TestStructuredExceptions();
    TestFloatingStateAcrossFault();
    EmuTestCxxExceptions();
    TestMisalignedVectors();
    TestIndirectCalls();
    TestRv64X();

    printf("EMUTEST: END checks=%ld failures=%ld\n", EmuTestChecks, EmuTestFailures);
    DbgPrint("EMUTEST: END checks=%ld failures=%ld\n", EmuTestChecks, EmuTestFailures);
    if (EmuTestFailures) return 1;
    if (ArgumentCount == 3 && !strcmp(Arguments[1], "exit")) ExitProcess(strtoul(Arguments[2], NULL, 0));
    return 0;
}
