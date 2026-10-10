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
#include <sys/mman.h>
#include <semaphore.h>
#include "bridge.h"

typedef BOOLEAN (*FELIX86_NT_PROBE)(VOID);

BOOLEAN Felix86NtProbeV(VOID);
BOOLEAN Felix86NtProbeZba(VOID);
BOOLEAN Felix86NtProbeZbb(VOID);
BOOLEAN Felix86NtProbeZbs(VOID);
BOOLEAN Felix86NtProbeZbc(VOID);
BOOLEAN Felix86NtProbeZicond(VOID);
BOOLEAN Felix86NtProbeZfa(VOID);
BOOLEAN Felix86NtProbeZacas(VOID);
BOOLEAN Felix86NtProbeZabha(VOID);
BOOLEAN Felix86NtProbeZvbb(VOID);
BOOLEAN Felix86NtProbeZvbc(VOID);
BOOLEAN Felix86NtProbeZvkned(VOID);
BOOLEAN Felix86NtProbeZvknha(VOID);
BOOLEAN Felix86NtProbeZvfhmin(VOID);
BOOLEAN Felix86NtProbeZknd(VOID);
BOOLEAN Felix86NtProbeZicbom(VOID);
BOOLEAN Felix86NtProbeZicclsm(VOID);
ULONG Felix86NtReadVectorBytes(VOID);

static const struct
{
    ULONG Extension;
    ULONG Requires;
    FELIX86_NT_PROBE Probe;
} Felix86NtProbes[] =
{
    { FELIX86_NT_EXT_V, 0, Felix86NtProbeV },
    { FELIX86_NT_EXT_ZBA, 0, Felix86NtProbeZba },
    { FELIX86_NT_EXT_ZBB, 0, Felix86NtProbeZbb },
    { FELIX86_NT_EXT_ZBS, 0, Felix86NtProbeZbs },
    { FELIX86_NT_EXT_ZBC, 0, Felix86NtProbeZbc },
    { FELIX86_NT_EXT_ZICOND, 0, Felix86NtProbeZicond },
    { FELIX86_NT_EXT_ZFA, 0, Felix86NtProbeZfa },
    { FELIX86_NT_EXT_ZACAS, 0, Felix86NtProbeZacas },
    { FELIX86_NT_EXT_ZABHA, 0, Felix86NtProbeZabha },
    { FELIX86_NT_EXT_ZVBB, FELIX86_NT_EXT_V, Felix86NtProbeZvbb },
    { FELIX86_NT_EXT_ZVBC, FELIX86_NT_EXT_V, Felix86NtProbeZvbc },
    { FELIX86_NT_EXT_ZVKNED, FELIX86_NT_EXT_V, Felix86NtProbeZvkned },
    { FELIX86_NT_EXT_ZVKNHA, FELIX86_NT_EXT_V, Felix86NtProbeZvknha },
    { FELIX86_NT_EXT_ZVFHMIN, FELIX86_NT_EXT_V, Felix86NtProbeZvfhmin },
    { FELIX86_NT_EXT_ZKND, 0, Felix86NtProbeZknd },
    { FELIX86_NT_EXT_ZICBOM, 0, Felix86NtProbeZicbom },
    { FELIX86_NT_EXT_ZICCLSM, 0, Felix86NtProbeZicclsm },
};

static ULONG Felix86NtExtensions;
static int Felix86NtErrno;

uint32_t
Felix86NtProbeExtensions(void)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Felix86NtProbes); Index++)
    {
        if ((Felix86NtExtensions & Felix86NtProbes[Index].Requires) != Felix86NtProbes[Index].Requires) continue;

        _SEH2_TRY
        {
            if (Felix86NtProbes[Index].Probe()) Felix86NtExtensions |= Felix86NtProbes[Index].Extension;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }

    return Felix86NtExtensions;
}

uint32_t
Felix86NtVectorBytes(void)
{
    return (Felix86NtExtensions & FELIX86_NT_EXT_V) ? Felix86NtReadVectorBytes() : 0;
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

void *
mmap(void *Address, size_t Length, int Protection, int Flags, int File, int64_t Offset)
{
    PVOID Base = Address;
    SIZE_T Size = Length;
    ULONG Type;
    NTSTATUS Status;

    if (!Length || File != -1 || Offset || !(Flags & MAP_ANONYMOUS)) return MAP_FAILED;

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

    UNREFERENCED_PARAMETER(Length);
    return NT_SUCCESS(NtFreeVirtualMemory(NtCurrentProcess(), &Address, &Size, MEM_RELEASE)) ? 0 : -1;
}

int
mprotect(void *Address, size_t Length, int Protection)
{
    SIZE_T Size = Length;
    ULONG OldProtection;

    return NT_SUCCESS(NtProtectVirtualMemory(NtCurrentProcess(),
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
    UNREFERENCED_PARAMETER(Flags);
    NtFlushInstructionCache(NtCurrentProcess(), Start, (ULONG_PTR)End - (ULONG_PTR)Start);
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
