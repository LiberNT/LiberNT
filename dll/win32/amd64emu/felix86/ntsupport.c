/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System and C runtime services of the felix86 sources over NTDLL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../amd64emu.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#include <sys/cachectl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include "bridge.h"

#define KF_RISCV_ZICBOM  0x00000001
#define KF_RISCV_ZBA     0x00000020
#define KF_RISCV_ZBB     0x00000040
#define KF_RISCV_ZBS     0x00000080
#define KF_RISCV_V       0x00002000
#define KF_RISCV_ZBC     0x00010000
#define KF_RISCV_ZICOND  0x00020000
#define KF_RISCV_ZFA     0x00040000
#define KF_RISCV_ZACAS   0x00080000
#define KF_RISCV_ZABHA   0x00100000
#define KF_RISCV_ZVBB    0x00200000
#define KF_RISCV_ZVBC    0x00400000
#define KF_RISCV_ZVKNED  0x00800000
#define KF_RISCV_ZVKNHA  0x01000000
#define KF_RISCV_ZVFHMIN 0x02000000
#define KF_RISCV_ZKND    0x04000000
#define KF_RISCV_ZICCLSM 0x08000000

ULONG Felix86NtReadVectorBytes(VOID);

static const struct
{
    ULONG Extension;
    ULONG Requires;
    ULONG64 FeatureBit;
} Felix86NtFeatures[] =
{
    { FELIX86_NT_EXT_V, 0, KF_RISCV_V },
    { FELIX86_NT_EXT_ZBA, 0, KF_RISCV_ZBA },
    { FELIX86_NT_EXT_ZBB, 0, KF_RISCV_ZBB },
    { FELIX86_NT_EXT_ZBS, 0, KF_RISCV_ZBS },
    { FELIX86_NT_EXT_ZBC, 0, KF_RISCV_ZBC },
    { FELIX86_NT_EXT_ZICOND, 0, KF_RISCV_ZICOND },
    { FELIX86_NT_EXT_ZFA, 0, KF_RISCV_ZFA },
    { FELIX86_NT_EXT_ZACAS, 0, KF_RISCV_ZACAS },
    { FELIX86_NT_EXT_ZABHA, 0, KF_RISCV_ZABHA },
    { FELIX86_NT_EXT_ZVBB, FELIX86_NT_EXT_V, KF_RISCV_ZVBB },
    { FELIX86_NT_EXT_ZVBC, FELIX86_NT_EXT_V, KF_RISCV_ZVBC },
    { FELIX86_NT_EXT_ZVKNED, FELIX86_NT_EXT_V, KF_RISCV_ZVKNED },
    { FELIX86_NT_EXT_ZVKNHA, FELIX86_NT_EXT_V, KF_RISCV_ZVKNHA },
    { FELIX86_NT_EXT_ZVFHMIN, FELIX86_NT_EXT_V, KF_RISCV_ZVFHMIN },
    { FELIX86_NT_EXT_ZKND, 0, KF_RISCV_ZKND },
    { FELIX86_NT_EXT_ZICBOM, 0, KF_RISCV_ZICBOM },
    { FELIX86_NT_EXT_ZICCLSM, FELIX86_NT_EXT_V, KF_RISCV_ZICCLSM },
};

static ULONG Felix86NtExtensions;
static int Felix86NtErrno;
static PHANDLE Felix86NtTerminationThreads;
static ULONG Felix86NtTerminationThreadCount;
static BOOLEAN Felix86NtTerminationHeapLocked;

void
Felix86NtReportError(const char *Message, uint32_t Status)
{
    static LONG Reported;
    WCHAR Buffer[512];
    WCHAR CodeBuffer[16];
    UNICODE_STRING Text, Code, Caption = RTL_CONSTANT_STRING(L"Emulator error");
    ANSI_STRING Ansi;
    ULONG Response = ResponseNotHandled;
    ULONG_PTR Parameters[4] = { (ULONG_PTR)&Text, (ULONG_PTR)&Caption, 0x10, 0xFFFFFFFF };

    if (_InterlockedCompareExchange(&Reported, 1, 0)) return;
    RtlInitEmptyUnicodeString(&Text, Buffer, sizeof(Buffer));
    RtlAppendUnicodeToString(&Text, L"The emulator encountered an error.\r\n\r\n");
    RtlInitAnsiString(&Ansi, Message);
    RtlInitEmptyUnicodeString(&Code, CodeBuffer, sizeof(CodeBuffer));
    if (NT_SUCCESS(RtlAnsiStringToUnicodeString(&Code, &Ansi, TRUE)))
    {
        RtlAppendUnicodeStringToString(&Text, &Code);
        RtlFreeUnicodeString(&Code);
    }
    RtlAppendUnicodeToString(&Text, L"\r\n\r\nError status: 0x");
    RtlInitEmptyUnicodeString(&Code, CodeBuffer, sizeof(CodeBuffer));
    if (NT_SUCCESS(RtlIntegerToUnicodeString(Status, 16, &Code)))
        RtlAppendUnicodeStringToString(&Text, &Code);
    DbgPrint("FELIX86: %s: %08lx\n", Message, (ULONG)Status);
    NtRaiseHardError(STATUS_SERVICE_NOTIFICATION | HARDERROR_OVERRIDE_ERRORMODE,
                     RTL_NUMBER_OF(Parameters), 3, Parameters, OptionOk, &Response);
}

uint32_t
Felix86NtProbeExtensions(void)
{
    SYSTEM_PROCESSOR_INFORMATION Information;
    ULONG Index;

    if (!NT_SUCCESS(NtQuerySystemInformation(SystemProcessorInformation, &Information, sizeof(Information), NULL)))
        return Felix86NtExtensions;

    for (Index = 0; Index < RTL_NUMBER_OF(Felix86NtFeatures); Index++)
    {
        if ((Felix86NtExtensions & Felix86NtFeatures[Index].Requires) != Felix86NtFeatures[Index].Requires) continue;
        if (Information.ProcessorFeatureBits & Felix86NtFeatures[Index].FeatureBit)
            Felix86NtExtensions |= Felix86NtFeatures[Index].Extension;
    }

    return Felix86NtExtensions;
}

uint32_t
Felix86NtVectorBytes(void)
{
    return (Felix86NtExtensions & FELIX86_NT_EXT_V) ? Felix86NtReadVectorBytes() : 0;
}

int
Felix86NtRelocatableCode(void)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"FELIX86_RELOCATABLE");
    WCHAR Buffer[4];
    UNICODE_STRING Value = { 0, sizeof(Buffer), Buffer };

    return NT_SUCCESS(RtlQueryEnvironmentVariable_U(NULL, &Name, &Value)) && Value.Length == sizeof(WCHAR) &&
           Buffer[0] == L'1';
}

void
Felix86NtPrint(const char *Text)
{
    DbgPrint("felix86: %s", Text);
}

void
Felix86NtTerminate(uint32_t Status)
{
    DbgPrint("felix86: terminating the process: %08lx\n", Status);
    NtTerminateProcess(NtCurrentProcess(), Status);
    for (;;) NtYieldExecution();
}

void
Felix86NtYield(void)
{
    NtYieldExecution();
}

void
Felix86NtBeginTermination(void)
{
    HANDLE Thread = NULL;
    PHANDLE Threads = NULL, Grown;
    ULONG Count = 0, Capacity = 0;

    Felix86NtCacheFlush();

    while (NT_SUCCESS(NtGetNextThread(NtCurrentProcess(), Thread, SYNCHRONIZE | THREAD_QUERY_LIMITED_INFORMATION, 0, 0, &Thread)))
    {
        if (Count == Capacity)
        {
            Capacity = Capacity ? Capacity * 2 : 64;
            Grown = Threads ? RtlReAllocateHeap(RtlGetProcessHeap(), 0, Threads, Capacity * sizeof(HANDLE))
                            : RtlAllocateHeap(RtlGetProcessHeap(), 0, Capacity * sizeof(HANDLE));
            if (!Grown)
            {
                NtClose(Thread);
                break;
            }
            Threads = Grown;
        }
        Threads[Count++] = Thread;
    }

    Felix86NtTerminationThreads = Threads;
    Felix86NtTerminationThreadCount = Count;
    Felix86NtLockForTermination();
    Felix86NtTerminationHeapLocked = RtlLockHeap(RtlGetProcessHeap());
}

void
Felix86NtEndTermination(int Terminated)
{
    THREAD_BASIC_INFORMATION Current, Information;
    ULONG Index;

    if (!NT_SUCCESS(NtQueryInformationThread(NtCurrentThread(), ThreadBasicInformation, &Current, sizeof(Current), NULL)))
        Terminated = FALSE;

    for (Index = 0; Index < Felix86NtTerminationThreadCount; Index++)
    {
        if (Terminated &&
            NT_SUCCESS(NtQueryInformationThread(Felix86NtTerminationThreads[Index], ThreadBasicInformation,
                                                &Information, sizeof(Information), NULL)) &&
            Information.ClientId.UniqueThread != Current.ClientId.UniqueThread)
        {
            NtWaitForSingleObject(Felix86NtTerminationThreads[Index], FALSE, NULL);
        }
        NtClose(Felix86NtTerminationThreads[Index]);
    }

    if (Felix86NtTerminationHeapLocked) RtlUnlockHeap(RtlGetProcessHeap());
    Felix86NtUnlockAfterTermination();
    if (Felix86NtTerminationThreads) RtlFreeHeap(RtlGetProcessHeap(), 0, Felix86NtTerminationThreads);
    Felix86NtTerminationThreads = NULL;
    Felix86NtTerminationThreadCount = 0;
    Felix86NtTerminationHeapLocked = FALSE;
}

int
Felix86NtThreadId(void)
{
    return (int)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;
}

int
Felix86NtProcessId(void)
{
    return (int)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueProcess;
}

static
ULONG
Felix86NtProtection(int Protection)
{
    static const ULONG Protections[8] =
    {
        PAGE_NOACCESS, PAGE_READONLY, PAGE_READWRITE, PAGE_READWRITE,
        PAGE_EXECUTE, PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_READWRITE
    };

    return Protections[Protection & 7];
}

#define F86_LAZY_MINIMUM (256 * 1024)
#define F86_LAZY_GRANULE (64 * 1024)
#define F86_LAZY_REGIONS 4096

typedef struct _F86_LAZY_REGION
{
    ULONG_PTR Start;
    ULONG_PTR End;
    ULONG Protect;
} F86_LAZY_REGION;

static F86_LAZY_REGION F86LazyRegions[F86_LAZY_REGIONS];
static ULONG F86LazyCount;
static RTL_SRWLOCK F86LazyLock = RTL_SRWLOCK_INIT;
static volatile LONG F86LazyHandler;

static
LONG
NTAPI
F86LazyFault(
    _In_ PEXCEPTION_POINTERS Pointers)
{
    PEXCEPTION_RECORD Record = Pointers->ExceptionRecord;
    MEMORY_BASIC_INFORMATION Information;
    ULONG_PTR Address, Start = 0, End = 0, Granule;
    ULONG Protect = 0, Index;
    PVOID Base;
    SIZE_T Size;

    if (Record->ExceptionCode != STATUS_ACCESS_VIOLATION || Record->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
    Address = Record->ExceptionInformation[1];

    RtlAcquireSRWLockShared(&F86LazyLock);
    for (Index = 0; Index < F86LazyCount; Index++)
    {
        if (Address >= F86LazyRegions[Index].Start && Address < F86LazyRegions[Index].End)
        {
            Start = F86LazyRegions[Index].Start;
            End = F86LazyRegions[Index].End;
            Protect = F86LazyRegions[Index].Protect;
            break;
        }
    }
    RtlReleaseSRWLockShared(&F86LazyLock);
    if (!End)
        return EXCEPTION_CONTINUE_SEARCH;

    if (!NT_SUCCESS(ZwQueryVirtualMemory(NtCurrentProcess(), (PVOID)Address, MemoryBasicInformation, &Information,
                                         sizeof(Information), NULL)) ||
        Information.State != MEM_RESERVE)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    Granule = (Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
                  ? F86_LAZY_GRANULE : PAGE_SIZE;
    Base = (PVOID)max(Start, Address & ~(ULONG_PTR)(Granule - 1));
    Size = min(End, (Address & ~(ULONG_PTR)(Granule - 1)) + Granule) - (ULONG_PTR)Base;
    if (!NT_SUCCESS(ZwAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size, MEM_COMMIT, Protect)))
        return EXCEPTION_CONTINUE_SEARCH;
    return EXCEPTION_CONTINUE_EXECUTION;
}

int
Felix86NtHostFault(
    void *Pointers)
{
    return F86LazyFault(Pointers) == EXCEPTION_CONTINUE_EXECUTION || Felix86NtCodeWatchFault(Pointers);
}

static
BOOLEAN
F86LazyRemember(
    _In_ PVOID Base,
    _In_ SIZE_T Size,
    _In_ ULONG Protect)
{
    BOOLEAN Remembered = FALSE;
    HANDLE DebugPort = NULL;

    /* TODO: mappings remembered before a debugger attaches still fault into that debugger on first touch */
    if (!NT_SUCCESS(ZwQueryInformationProcess(NtCurrentProcess(), ProcessDebugPort, &DebugPort, sizeof(DebugPort),
                                              NULL)) ||
        DebugPort != NULL)
    {
        return FALSE;
    }

    if (!InterlockedCompareExchange(&F86LazyHandler, 1, 0) && !RtlAddVectoredExceptionHandler(1, F86LazyFault))
        InterlockedExchange(&F86LazyHandler, 0);
    if (!F86LazyHandler)
        return FALSE;

    RtlAcquireSRWLockExclusive(&F86LazyLock);
    if (F86LazyCount < F86_LAZY_REGIONS)
    {
        F86LazyRegions[F86LazyCount].Start = (ULONG_PTR)Base;
        F86LazyRegions[F86LazyCount].End = (ULONG_PTR)Base + Size;
        F86LazyRegions[F86LazyCount].Protect = Protect;
        F86LazyCount++;
        Remembered = TRUE;
    }
    RtlReleaseSRWLockExclusive(&F86LazyLock);
    return Remembered;
}

static
VOID
F86LazyForget(
    _In_ PVOID Address,
    _In_ SIZE_T Length)
{
    ULONG_PTR Start = (ULONG_PTR)Address, End = Start + Length;
    ULONG Index;

    RtlAcquireSRWLockExclusive(&F86LazyLock);
    for (Index = 0; Index < F86LazyCount;)
    {
        if (F86LazyRegions[Index].Start >= Start && F86LazyRegions[Index].Start < End)
            F86LazyRegions[Index] = F86LazyRegions[--F86LazyCount];
        else
            Index++;
    }
    RtlReleaseSRWLockExclusive(&F86LazyLock);
}

void *
mmap(void *Address, size_t Length, int Protection, int Flags, int File, int64_t Offset)
{
    PVOID Base = Address;
    SIZE_T Size = Length;
    ULONG Type;
    NTSTATUS Status;

    if (!Length || File != -1 || Offset || !(Flags & MAP_ANONYMOUS)) return MAP_FAILED;

    if (Protection != PROT_NONE && Length >= F86_LAZY_MINIMUM)
    {
        if (!(Flags & MAP_FIXED))
        {
            Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size, MEM_RESERVE,
                                             Felix86NtProtection(Protection));
            if (!NT_SUCCESS(Status)) return MAP_FAILED;
        }
        if (F86LazyRemember(Base, Size, Felix86NtProtection(Protection))) return Base;
        if (!(Flags & MAP_FIXED))
        {
            Status = ZwAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size, MEM_COMMIT,
                                             Felix86NtProtection(Protection));
            return NT_SUCCESS(Status) ? Base : MAP_FAILED;
        }
    }

    if (Protection == PROT_NONE)
        Type = MEM_RESERVE;
    else if (Flags & MAP_FIXED)
        Type = MEM_COMMIT;
    else
        Type = MEM_RESERVE | MEM_COMMIT;

    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Base, 0, &Size, Type, Felix86NtProtection(Protection));
    if (!NT_SUCCESS(Status)) return MAP_FAILED;
    return Base;
}

int
munmap(void *Address, size_t Length)
{
    SIZE_T Size = 0;

    F86LazyForget(Address, Length);

    UNREFERENCED_PARAMETER(Length);
    return NT_SUCCESS(ZwFreeVirtualMemory(NtCurrentProcess(), &Address, &Size, MEM_RELEASE)) ? 0 : -1;
}

int
mprotect(void *Address, size_t Length, int Protection)
{
    SIZE_T Size = Length;
    ULONG OldProtection;

    return NT_SUCCESS(ZwProtectVirtualMemory(NtCurrentProcess(),
                                             &Address,
                                             &Size,
                                             Felix86NtProtection(Protection),
                                             &OldProtection)) ? 0 : -1;
}

int
madvise(void *Address, size_t Length, int Advice)
{
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(Advice);
    return 0;
}

void
__riscv_flush_icache(void *Start, void *End, unsigned long Flags)
{
    if (Flags & SYS_RISCV_FLUSH_ICACHE_LOCAL)
    {
        __asm__ __volatile__("fence.i" ::: "memory");
        return;
    }
    ZwFlushInstructionCache(NtCurrentProcess(), Start, (ULONG_PTR)End - (ULONG_PTR)Start);
}

int
sem_init(sem_t *Semaphore, int Shared, unsigned int Value)
{
    UNREFERENCED_PARAMETER(Shared);
    UNREFERENCED_PARAMETER(Value);
    RtlInitializeSRWLock((PRTL_SRWLOCK)&Semaphore->Lock);
    return 0;
}

int
sem_wait(sem_t *Semaphore)
{
    RtlAcquireSRWLockExclusive((PRTL_SRWLOCK)&Semaphore->Lock);
    return 0;
}

int
sem_post(sem_t *Semaphore)
{
    RtlReleaseSRWLockExclusive((PRTL_SRWLOCK)&Semaphore->Lock);
    return 0;
}

long
syscall(long Number, ...)
{
    UNREFERENCED_PARAMETER(Number);
    return 0;
}

int
ioctl(int File, unsigned long Request, ...)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Request);
    return -1;
}

int
open(const char *Path, int Flags, ...)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Flags);
    return -1;
}

int
close(int File)
{
    UNREFERENCED_PARAMETER(File);
    return -1;
}

int
read(int File, void *Buffer, unsigned int Length)
{
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    return -1;
}

double
__acrt_math_error(int Type, const char *Name, double First, double Second, double Result)
{
    UNREFERENCED_PARAMETER(Type);
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(First);
    UNREFERENCED_PARAMETER(Second);
    return Result;
}

void
sincos(double Value, double *Sine, double *Cosine)
{
    *Sine = sin(Value);
    *Cosine = cos(Value);
}

void *
malloc(size_t Size)
{
    return RtlAllocateHeap(RtlGetProcessHeap(), 0, Size);
}

void *
calloc(size_t Count, size_t Size)
{
    if (Size && Count > MAXSIZE_T / Size) return NULL;
    return RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Count * Size);
}

void *
realloc(void *Block, size_t Size)
{
    if (!Block) return malloc(Size);
    return RtlReAllocateHeap(RtlGetProcessHeap(), 0, Block, Size);
}

void
free(void *Block)
{
    if (Block) RtlFreeHeap(RtlGetProcessHeap(), 0, Block);
}

void
abort(void)
{
    Felix86NtTerminate(STATUS_FATAL_APP_EXIT);
}

void
exit(int Code)
{
    UNREFERENCED_PARAMETER(Code);
    Felix86NtTerminate(STATUS_FATAL_APP_EXIT);
}

int
atexit(void (*Function)(void))
{
    UNREFERENCED_PARAMETER(Function);
    return 0;
}

int *
_errno(void)
{
    return &Felix86NtErrno;
}

char *
strerror(int Error)
{
    UNREFERENCED_PARAMETER(Error);
    return "host operation failed";
}

FILE _iob[3];

FILE *
__acrt_iob_func(unsigned Index)
{
    return &_iob[Index < RTL_NUMBER_OF(_iob) ? Index : 2];
}

size_t
fwrite(const void *Buffer, size_t Size, size_t Count, FILE *File)
{
    UNREFERENCED_PARAMETER(File);
    DbgPrint("felix86: %.*s", (int)(Size * Count), (const char *)Buffer);
    return Count;
}

int
__stdio_common_vfprintf(unsigned __int64 Options, FILE *File, const char *Format, _locale_t Locale, va_list Arguments)
{
    char Buffer[512];
    int Length;

    UNREFERENCED_PARAMETER(Options);
    UNREFERENCED_PARAMETER(File);
    UNREFERENCED_PARAMETER(Locale);
    Length = _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    Buffer[sizeof(Buffer) - 1] = 0;
    DbgPrint("felix86: %s", Buffer);
    return Length;
}

int
__stdio_common_vsprintf(unsigned __int64 Options, char *Buffer, size_t Count, const char *Format, _locale_t Locale, va_list Arguments)
{
    int Length;

    UNREFERENCED_PARAMETER(Options);
    UNREFERENCED_PARAMETER(Locale);
    if (!Buffer || !Count) return _vsnprintf(NULL, 0, Format, Arguments);
    Length = _vsnprintf(Buffer, Count, Format, Arguments);
    if (Length < 0 || (size_t)Length >= Count)
    {
        Buffer[Count - 1] = 0;
        return -1;
    }
    return Length;
}

int
__stdio_common_vswprintf(unsigned __int64 Options, wchar_t *Buffer, size_t Count, const wchar_t *Format, _locale_t Locale, va_list Arguments)
{
    int Length;

    UNREFERENCED_PARAMETER(Options);
    UNREFERENCED_PARAMETER(Locale);
    if (!Buffer || !Count) return -1;
    Length = _vsnwprintf(Buffer, Count, Format, Arguments);
    if (Length < 0 || (size_t)Length >= Count)
    {
        Buffer[Count - 1] = 0;
        return -1;
    }
    return Length;
}

__int64
strtoll(const char *Text, char **End, int Base)
{
    return _strtoi64(Text, End, Base);
}

__int64
wcstoll(const wchar_t *Text, wchar_t **End, int Base)
{
    return _wcstoi64(Text, End, Base);
}

unsigned __int64
wcstoull(const wchar_t *Text, wchar_t **End, int Base)
{
    return _wcstoui64(Text, End, Base);
}

double
strtod(const char *Text, char **End)
{
    UNREFERENCED_PARAMETER(Text);
    UNREFERENCED_PARAMETER(End);
    Felix86NtTerminate(STATUS_NOT_IMPLEMENTED);
}

float
strtof(const char *Text, char **End)
{
    return (float)strtod(Text, End);
}

long double
strtold(const char *Text, char **End)
{
    return strtod(Text, End);
}

double
wcstod(const wchar_t *Text, wchar_t **End)
{
    UNREFERENCED_PARAMETER(Text);
    UNREFERENCED_PARAMETER(End);
    Felix86NtTerminate(STATUS_NOT_IMPLEMENTED);
}

float
wcstof(const wchar_t *Text, wchar_t **End)
{
    return (float)wcstod(Text, End);
}

long double
wcstold(const wchar_t *Text, wchar_t **End)
{
    return wcstod(Text, End);
}
