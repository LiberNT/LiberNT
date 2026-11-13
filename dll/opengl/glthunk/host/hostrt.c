/*
 * PROJECT:     LiberNT OpenGL thunk
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process services of the native OpenGL host inside an emulation host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#define NTOS_MODE_USER
#define _KERNEL32_
#define _USER32_
#include <stdarg.h>
#include <stddef.h>
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <winnls.h>
#include <tlhelp32.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ndk/ntndk.h>
#include <reactos/emuhost.h>

#define HOSTRT_EOF          (-1)
#define HOSTRT_ENOENT       2
#define HOSTRT_EBADF        9
#define HOSTRT_EAGAIN       11
#define HOSTRT_EINVAL       22
#define HOSTRT_ERANGE       34
#define HOSTRT_FLOAT_DIGITS 768
#define HOSTRT_BIG_LIMBS    128
#define HOSTRT_STANDARD_SNPRINTF 2
#define HOSTRT_ALIGNMENT    16

typedef struct _HOSTRT_ALIGNED
{
    PVOID Base;
    SIZE_T Size;
} HOSTRT_ALIGNED;

typedef struct _HOSTRT_VARIABLE
{
    struct _HOSTRT_VARIABLE *Next;
    PSTR Name;
    PSTR Value;
} HOSTRT_VARIABLE;

typedef void (__cdecl *HOSTRT_PVFV)(void);
typedef int (__cdecl *HOSTRT_COMPARE_S)(void *, const void *, const void *);

int __cdecl ChpeVsscanf(const char *String, const char *Format, va_list Arguments);
int __cdecl _vscprintf(const char *Format, va_list Arguments);
int __cdecl _vsnprintf(char *Buffer, size_t Count, const char *Format, va_list Arguments);
__int64 __cdecl _strtoi64(const char *String, char **End, int Base);
__int64 __cdecl _wcstoi64(const wchar_t *String, wchar_t **End, int Base);
unsigned __int64 __cdecl _wcstoui64(const wchar_t *String, wchar_t **End, int Base);
int __cdecl _vsnwprintf(wchar_t *Buffer, size_t Count, const wchar_t *Format, va_list Arguments);
char * __cdecl strtok_s(char *String, const char *Delimiters, char **Context);
size_t __cdecl strlen(const char *String);
int __cdecl strcmp(const char *Left, const char *Right);
int __cdecl _stricmp(const char *Left, const char *Right);
char * __cdecl strcpy(char *Destination, const char *Source);
void * __cdecl memcpy(void *Destination, const void *Source, size_t Count);
double __cdecl log(double Value);
NTSYSAPI NTSTATUS NTAPI RtlWaitOnAddress(const void *Address, const void *Compare, SIZE_T Size,
                                         const LARGE_INTEGER *Timeout);
NTSYSAPI VOID NTAPI RtlWakeAddressSingle(const void *Address);
NTSYSAPI VOID NTAPI RtlWakeAddressAll(const void *Address);

FILE _iob[3];

static INT HostRtSystemErrorCount;
static RTL_CRITICAL_SECTION HostRtLock;
static RTL_RUN_ONCE HostRtLockOnce = RTL_RUN_ONCE_INIT;
static RTL_SRWLOCK HostRtEnvironmentLock = RTL_SRWLOCK_INIT;
static HOSTRT_VARIABLE *HostRtVariables;
static DWORD HostRtErrnoSlot = TLS_OUT_OF_INDEXES;
static DWORD HostRtStrtokSlot = TLS_OUT_OF_INDEXES;
static RTL_RUN_ONCE HostRtSlotsOnce = RTL_RUN_ONCE_INIT;
static const UCHAR HostRtGuestOpenGl = 0;

static
PLARGE_INTEGER
HostRtTimeout(
    _Out_ PLARGE_INTEGER Timeout,
    _In_ DWORD Milliseconds)
{
    if (Milliseconds == INFINITE) return NULL;
    Timeout->QuadPart = -(LONGLONG)Milliseconds * 10000;
    return Timeout;
}

static
VOID
HostRtSetStatus(
    _In_ NTSTATUS Status)
{
    NtCurrentTeb()->LastErrorValue = RtlNtStatusToDosError(Status);
}

static
ULONG64
HostRtCallGuest(
    _In_ PCSTR Module,
    _In_ PCSTR Name,
    _Inout_ ULONG64 *Cache,
    _In_ PCSTR Signature,
    _In_opt_ const ULONG64 *Arguments)
{
    const EMU_HOST_INTERFACE *Interface = EmuHostGetInterface();
    ULONG64 Function = *(volatile ULONG64 *)Cache;

    if (!Function)
    {
        Function = Interface->GuestProcedure(Module, Name);
        *(volatile ULONG64 *)Cache = Function;
    }
    if (!Function)
    {
        NtCurrentTeb()->LastErrorValue = ERROR_PROC_NOT_FOUND;
        return 0;
    }
    return Interface->CallGuest(Function, Signature, Arguments);
}

#define HOSTRT_GUEST(Module, Name, Signature, ...) \
    do \
    { \
        static ULONG64 Cache; \
        const ULONG64 Arguments[] = { __VA_ARGS__ }; \
        return (HOSTRT_RESULT)HostRtCallGuest(Module, Name, &Cache, Signature, Arguments); \
    } while (0)

static
ULONG
NTAPI
HostRtInitializeLock(
    _Inout_ PRTL_RUN_ONCE RunOnce,
    _Inout_opt_ PVOID Parameter,
    _Inout_opt_ PVOID *Context)
{
    return NT_SUCCESS(RtlInitializeCriticalSection(&HostRtLock));
}

static
ULONG
NTAPI
HostRtInitializeSlots(
    _Inout_ PRTL_RUN_ONCE RunOnce,
    _Inout_opt_ PVOID Parameter,
    _Inout_opt_ PVOID *Context)
{
    HostRtErrnoSlot = TlsAlloc();
    HostRtStrtokSlot = TlsAlloc();
    return HostRtErrnoSlot != TLS_OUT_OF_INDEXES && HostRtStrtokSlot != TLS_OUT_OF_INDEXES;
}

static
PVOID *
HostRtSlot(
    _In_ PDWORD Index)
{
    static PVOID Fallback;

    RtlRunOnceExecuteOnce(&HostRtSlotsOnce, HostRtInitializeSlots, NULL, NULL);
    return *Index < TLS_MINIMUM_AVAILABLE ? &NtCurrentTeb()->TlsSlots[*Index] : &Fallback;
}


void * __cdecl
malloc(size_t Size)
{
    return RtlAllocateHeap(RtlGetProcessHeap(), 0, Size ? Size : 1);
}

void * __cdecl
calloc(size_t Count, size_t Size)
{
    if (Size && Count > MAXSIZE_T / Size) return NULL;
    return RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Count * Size ? Count * Size : 1);
}

void __cdecl
free(void *Block)
{
    if (Block) RtlFreeHeap(RtlGetProcessHeap(), 0, Block);
}

void * __cdecl
realloc(void *Block, size_t Size)
{
    if (!Block) return malloc(Size);
    if (!Size)
    {
        free(Block);
        return NULL;
    }
    return RtlReAllocateHeap(RtlGetProcessHeap(), 0, Block, Size);
}

void * __cdecl
_aligned_malloc(size_t Size, size_t Alignment)
{
    HOSTRT_ALIGNED *Header;
    ULONG_PTR Base, Aligned;

    if (Alignment < HOSTRT_ALIGNMENT) Alignment = HOSTRT_ALIGNMENT;
    if (Alignment & (Alignment - 1)) return NULL;
    Base = (ULONG_PTR)malloc(Size + Alignment + sizeof(HOSTRT_ALIGNED));
    if (!Base) return NULL;
    Aligned = (Base + sizeof(HOSTRT_ALIGNED) + Alignment - 1) & ~(Alignment - 1);
    Header = (HOSTRT_ALIGNED *)Aligned - 1;
    Header->Base = (PVOID)Base;
    Header->Size = Size;
    return (PVOID)Aligned;
}

void __cdecl
_aligned_free(void *Block)
{
    if (Block) free(((HOSTRT_ALIGNED *)Block - 1)->Base);
}

void * __cdecl
_aligned_realloc(void *Block, size_t Size, size_t Alignment)
{
    PVOID New;

    if (!Block) return _aligned_malloc(Size, Alignment);
    if (!Size)
    {
        _aligned_free(Block);
        return NULL;
    }
    New = _aligned_malloc(Size, Alignment);
    if (!New) return NULL;
    memcpy(New, Block, min(Size, ((HOSTRT_ALIGNED *)Block - 1)->Size));
    _aligned_free(Block);
    return New;
}

char * __cdecl
_strdup(const char *String)
{
    size_t Length = strlen(String) + 1;
    char *Copy = malloc(Length);

    if (Copy) memcpy(Copy, String, Length);
    return Copy;
}

HLOCAL WINAPI
LocalFree(HLOCAL Memory)
{
    if (Memory && !RtlFreeHeap(RtlGetProcessHeap(), 0, Memory)) return Memory;
    return NULL;
}


int * __cdecl
_errno(void)
{
    return (int *)HostRtSlot(&HostRtErrnoSlot);
}

int * __cdecl
__sys_nerr(void)
{
    return &HostRtSystemErrorCount;
}

int __cdecl
strerror_s(char *Buffer, size_t Size, int Error)
{
    static const char Text[] = "unknown error";

    if (!Buffer || !Size) return HOSTRT_EINVAL;
    Buffer[0] = 0;
    if (Size >= sizeof(Text)) strcpy(Buffer, Text);
    return 0;
}

DWORD WINAPI
GetLastError(VOID)
{
    return NtCurrentTeb()->LastErrorValue;
}

VOID WINAPI
SetLastError(DWORD Error)
{
    NtCurrentTeb()->LastErrorValue = Error;
}

UINT WINAPI
SetErrorMode(UINT Mode)
{
    return 0;
}

DWORD WINAPI
FormatMessageA(DWORD Flags, LPCVOID Source, DWORD MessageId, DWORD LanguageId, LPSTR Buffer, DWORD Size,
               va_list *Arguments)
{
    NtCurrentTeb()->LastErrorValue = ERROR_RESOURCE_TYPE_NOT_FOUND;
    return 0;
}

VOID WINAPI
OutputDebugStringA(LPCSTR String)
{
    if (String) DbgPrint("%s", String);
}

BOOL WINAPI
IsDebuggerPresent(VOID)
{
    return NtCurrentPeb()->BeingDebugged;
}

VOID WINAPI
DebugBreak(VOID)
{
    DbgBreakPoint();
}

VOID WINAPI
RaiseException(DWORD Code, DWORD Flags, DWORD Count, CONST ULONG_PTR *Arguments)
{
    EXCEPTION_RECORD Record;

    RtlZeroMemory(&Record, sizeof(Record));
    Record.ExceptionCode = Code;
    Record.ExceptionFlags = Flags & EXCEPTION_NONCONTINUABLE;
    Record.ExceptionAddress = (PVOID)RaiseException;
    Record.NumberParameters = min(Count, EXCEPTION_MAXIMUM_PARAMETERS);
    if (Arguments) RtlCopyMemory(Record.ExceptionInformation, Arguments, Record.NumberParameters * sizeof(ULONG_PTR));
    RtlRaiseException(&Record);
}


DECLSPEC_NORETURN void __cdecl
abort(void)
{
    NtTerminateProcess(NtCurrentProcess(), STATUS_FATAL_APP_EXIT);
    for (;;) NtYieldExecution();
}

DECLSPEC_NORETURN void __cdecl
exit(int Code)
{
    NtTerminateProcess(NtCurrentProcess(), (NTSTATUS)Code);
    for (;;) NtYieldExecution();
}

DECLSPEC_NORETURN void __cdecl
_amsg_exit(int Code)
{
    DbgPrint("GLHOST: runtime error %d\n", Code);
    abort();
}

void __cdecl
_wassert(const wchar_t *Message, const wchar_t *File, unsigned Line)
{
    DbgPrint("GLHOST: assertion failed: %ls, %ls:%u\n", Message, File, Line);
    abort();
}

void (__cdecl *signal(int Signal, void (__cdecl *Handler)(int)))(int)
{
    return NULL;
}

void __cdecl
_initterm(HOSTRT_PVFV *Begin, HOSTRT_PVFV *End)
{
    for (; Begin < End; Begin++)
        if (*Begin) (*Begin)();
}

void __cdecl
_lock(int Lock)
{
    RtlRunOnceExecuteOnce(&HostRtLockOnce, HostRtInitializeLock, NULL, NULL);
    RtlEnterCriticalSection(&HostRtLock);
}

void __cdecl
_unlock(int Lock)
{
    RtlLeaveCriticalSection(&HostRtLock);
}

void __cdecl
_fpreset(void)
{
}

int __cdecl
_set_error_mode(int Mode)
{
    return 0;
}


static
PSTR
HostRtQueryVariable(
    _In_ PCSTR Name)
{
    ANSI_STRING NameAnsi, ValueAnsi;
    UNICODE_STRING NameUnicode, ValueUnicode;
    WCHAR Buffer[1024];
    PSTR Value;

    RtlInitAnsiString(&NameAnsi, Name);
    if (!NT_SUCCESS(RtlAnsiStringToUnicodeString(&NameUnicode, &NameAnsi, TRUE))) return NULL;
    RtlInitEmptyUnicodeString(&ValueUnicode, Buffer, sizeof(Buffer));
    if (!NT_SUCCESS(RtlQueryEnvironmentVariable_U(NULL, &NameUnicode, &ValueUnicode)) ||
        !NT_SUCCESS(RtlUnicodeStringToAnsiString(&ValueAnsi, &ValueUnicode, TRUE)))
    {
        RtlFreeUnicodeString(&NameUnicode);
        return NULL;
    }
    RtlFreeUnicodeString(&NameUnicode);

    Value = malloc(ValueAnsi.Length + 1);
    if (Value)
    {
        memcpy(Value, ValueAnsi.Buffer, ValueAnsi.Length);
        Value[ValueAnsi.Length] = 0;
    }
    RtlFreeAnsiString(&ValueAnsi);
    return Value;
}

char * __cdecl
getenv(const char *Name)
{
    HOSTRT_VARIABLE *Variable;
    PSTR Value;

    RtlAcquireSRWLockShared(&HostRtEnvironmentLock);
    for (Variable = HostRtVariables; Variable; Variable = Variable->Next)
        if (!strcmp(Variable->Name, Name)) break;
    RtlReleaseSRWLockShared(&HostRtEnvironmentLock);
    if (Variable) return Variable->Value;

    Value = HostRtQueryVariable(Name);
    if (!Value) return NULL;

    Variable = malloc(sizeof(*Variable));
    if (!Variable) return Value;
    Variable->Name = _strdup(Name);
    Variable->Value = Value;
    RtlAcquireSRWLockExclusive(&HostRtEnvironmentLock);
    Variable->Next = HostRtVariables;
    HostRtVariables = Variable;
    RtlReleaseSRWLockExclusive(&HostRtEnvironmentLock);
    return Value;
}

BOOL WINAPI
SetEnvironmentVariableA(LPCSTR Name, LPCSTR Value)
{
    ANSI_STRING NameAnsi, ValueAnsi;
    UNICODE_STRING NameUnicode, ValueUnicode = { 0 };
    HOSTRT_VARIABLE *Variable;
    NTSTATUS Status;

    RtlInitAnsiString(&NameAnsi, Name);
    Status = RtlAnsiStringToUnicodeString(&NameUnicode, &NameAnsi, TRUE);
    if (NT_SUCCESS(Status) && Value)
    {
        RtlInitAnsiString(&ValueAnsi, Value);
        Status = RtlAnsiStringToUnicodeString(&ValueUnicode, &ValueAnsi, TRUE);
        if (!NT_SUCCESS(Status)) RtlFreeUnicodeString(&NameUnicode);
    }
    if (NT_SUCCESS(Status))
    {
        Status = RtlSetEnvironmentVariable(NULL, &NameUnicode, Value ? &ValueUnicode : NULL);
        RtlFreeUnicodeString(&NameUnicode);
        if (Value) RtlFreeUnicodeString(&ValueUnicode);
    }
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return FALSE;
    }

    RtlAcquireSRWLockExclusive(&HostRtEnvironmentLock);
    for (Variable = HostRtVariables; Variable; Variable = Variable->Next)
        if (!strcmp(Variable->Name, Name)) Variable->Value = Value ? _strdup(Value) : NULL;
    RtlReleaseSRWLockExclusive(&HostRtEnvironmentLock);
    return TRUE;
}

LPSTR WINAPI
GetCommandLineA(VOID)
{
    static PSTR CommandLine;
    ANSI_STRING Ansi;

    if (!CommandLine &&
        NT_SUCCESS(RtlUnicodeStringToAnsiString(&Ansi, &NtCurrentPeb()->ProcessParameters->CommandLine, TRUE)))
    {
        CommandLine = Ansi.Buffer;
    }
    return CommandLine ? CommandLine : "";
}

DWORD WINAPI
GetEnvironmentVariableA(LPCSTR Name, LPSTR Buffer, DWORD Size)
{
    PSTR Value = getenv(Name);
    DWORD Length;

    if (!Value)
    {
        NtCurrentTeb()->LastErrorValue = ERROR_ENVVAR_NOT_FOUND;
        return 0;
    }
    Length = (DWORD)strlen(Value);
    if (!Buffer || Length >= Size) return Length + 1;
    memcpy(Buffer, Value, Length + 1);
    return Length;
}


DWORD WINAPI
GetCurrentThreadId(VOID)
{
    return HandleToUlong(NtCurrentTeb()->ClientId.UniqueThread);
}

DWORD WINAPI
GetCurrentProcessId(VOID)
{
    return HandleToUlong(NtCurrentTeb()->ClientId.UniqueProcess);
}

HANDLE WINAPI
GetCurrentProcess(VOID)
{
    return NtCurrentProcess();
}

HANDLE WINAPI
GetCurrentThread(VOID)
{
    return NtCurrentThread();
}

DWORD WINAPI
GetCurrentProcessorNumber(VOID)
{
    return RtlGetCurrentProcessorNumber();
}

DWORD WINAPI
GetThreadId(HANDLE Thread)
{
    THREAD_BASIC_INFORMATION Information;
    NTSTATUS Status;

    Status = NtQueryInformationThread(Thread, ThreadBasicInformation, &Information, sizeof(Information), NULL);
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return 0;
    }
    return HandleToUlong(Information.ClientId.UniqueThread);
}

BOOL WINAPI
GetExitCodeThread(HANDLE Thread, LPDWORD ExitCode)
{
    THREAD_BASIC_INFORMATION Information;
    NTSTATUS Status;

    Status = NtQueryInformationThread(Thread, ThreadBasicInformation, &Information, sizeof(Information), NULL);
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return FALSE;
    }
    *ExitCode = Information.ExitStatus;
    return TRUE;
}

DWORD WINAPI
ResumeThread(HANDLE Thread)
{
    ULONG Previous;
    NTSTATUS Status = NtResumeThread(Thread, &Previous);

    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return (DWORD)-1;
    }
    return Previous;
}

DWORD_PTR WINAPI
SetThreadAffinityMask(HANDLE Thread, DWORD_PTR Mask)
{
    THREAD_BASIC_INFORMATION Information;
    KAFFINITY Affinity = Mask;
    NTSTATUS Status;

    Status = NtQueryInformationThread(Thread, ThreadBasicInformation, &Information, sizeof(Information), NULL);
    if (NT_SUCCESS(Status))
        Status = NtSetInformationThread(Thread, ThreadAffinityMask, &Affinity, sizeof(Affinity));
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return 0;
    }
    return Information.AffinityMask;
}

BOOL WINAPI
GetThreadTimes(HANDLE Thread, LPFILETIME Creation, LPFILETIME Exit, LPFILETIME Kernel, LPFILETIME User)
{
    KERNEL_USER_TIMES Times;
    NTSTATUS Status;

    Status = NtQueryInformationThread(Thread, ThreadTimes, &Times, sizeof(Times), NULL);
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return FALSE;
    }
    *(PLARGE_INTEGER)Creation = Times.CreateTime;
    *(PLARGE_INTEGER)Exit = Times.ExitTime;
    *(PLARGE_INTEGER)Kernel = Times.KernelTime;
    *(PLARGE_INTEGER)User = Times.UserTime;
    return TRUE;
}

BOOL WINAPI
GetProcessTimes(HANDLE Process, LPFILETIME Creation, LPFILETIME Exit, LPFILETIME Kernel, LPFILETIME User)
{
    KERNEL_USER_TIMES Times;
    NTSTATUS Status;

    Status = NtQueryInformationProcess(Process, ProcessTimes, &Times, sizeof(Times), NULL);
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return FALSE;
    }
    *(PLARGE_INTEGER)Creation = Times.CreateTime;
    *(PLARGE_INTEGER)Exit = Times.ExitTime;
    *(PLARGE_INTEGER)Kernel = Times.KernelTime;
    *(PLARGE_INTEGER)User = Times.UserTime;
    return TRUE;
}

/* TODO: start native threads inside the emulation host */
ULONG_PTR __cdecl
_beginthreadex(void *Security, unsigned StackSize, unsigned (__stdcall *Start)(void *), void *Argument,
               unsigned Flags, unsigned *ThreadId)
{
    *_errno() = HOSTRT_EAGAIN;
    return 0;
}

HANDLE WINAPI
CreateToolhelp32Snapshot(DWORD Flags, DWORD ProcessId)
{
    NtCurrentTeb()->LastErrorValue = ERROR_NOT_SUPPORTED;
    return INVALID_HANDLE_VALUE;
}

BOOL WINAPI
Thread32First(HANDLE Snapshot, LPTHREADENTRY32 Entry)
{
    NtCurrentTeb()->LastErrorValue = ERROR_NO_MORE_FILES;
    return FALSE;
}

BOOL WINAPI
Thread32Next(HANDLE Snapshot, LPTHREADENTRY32 Entry)
{
    NtCurrentTeb()->LastErrorValue = ERROR_NO_MORE_FILES;
    return FALSE;
}

DWORD WINAPI
TlsAlloc(VOID)
{
    ULONG Index;

    RtlAcquirePebLock();
    Index = RtlFindClearBitsAndSet(NtCurrentPeb()->TlsBitmap, 1, 0);
    RtlReleasePebLock();
    if (Index == MAXULONG)
    {
        NtCurrentTeb()->LastErrorValue = ERROR_NO_MORE_ITEMS;
        return TLS_OUT_OF_INDEXES;
    }
    NtCurrentTeb()->TlsSlots[Index] = NULL;
    return Index;
}

BOOL WINAPI
TlsFree(DWORD Index)
{
    if (Index >= TLS_MINIMUM_AVAILABLE)
    {
        NtCurrentTeb()->LastErrorValue = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    RtlAcquirePebLock();
    RtlClearBits(NtCurrentPeb()->TlsBitmap, Index, 1);
    RtlReleasePebLock();
    return TRUE;
}

LPVOID WINAPI
TlsGetValue(DWORD Index)
{
    if (Index >= TLS_MINIMUM_AVAILABLE)
    {
        NtCurrentTeb()->LastErrorValue = ERROR_INVALID_PARAMETER;
        return NULL;
    }
    NtCurrentTeb()->LastErrorValue = ERROR_SUCCESS;
    return NtCurrentTeb()->TlsSlots[Index];
}

BOOL WINAPI
TlsSetValue(DWORD Index, LPVOID Value)
{
    if (Index >= TLS_MINIMUM_AVAILABLE)
    {
        NtCurrentTeb()->LastErrorValue = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    NtCurrentTeb()->TlsSlots[Index] = Value;
    return TRUE;
}

DWORD WINAPI
FlsAlloc(PFLS_CALLBACK_FUNCTION Callback)
{
    ULONG Index;
    NTSTATUS Status = RtlFlsAlloc(Callback, &Index);

    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return FLS_OUT_OF_INDEXES;
    }
    return Index;
}

BOOL WINAPI
FlsFree(DWORD Index)
{
    NTSTATUS Status = RtlFlsFree(Index);

    if (!NT_SUCCESS(Status)) HostRtSetStatus(Status);
    return NT_SUCCESS(Status);
}

PVOID WINAPI
FlsGetValue(DWORD Index)
{
    PVOID Value;
    NTSTATUS Status = RtlFlsGetValue(Index, &Value);

    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return NULL;
    }
    NtCurrentTeb()->LastErrorValue = ERROR_SUCCESS;
    return Value;
}

BOOL WINAPI
FlsSetValue(DWORD Index, PVOID Value)
{
    NTSTATUS Status = RtlFlsSetValue(Index, Value);

    if (!NT_SUCCESS(Status)) HostRtSetStatus(Status);
    return NT_SUCCESS(Status);
}

BOOL WINAPI
SwitchToThread(VOID)
{
    return NtYieldExecution() != STATUS_NO_YIELD_PERFORMED;
}

void __cdecl
_endthreadex(unsigned Code)
{
    NtTerminateThread(NtCurrentThread(), (NTSTATUS)Code);
}

BOOL WINAPI
TryEnterCriticalSection(LPCRITICAL_SECTION Section)
{
    return RtlTryEnterCriticalSection((PRTL_CRITICAL_SECTION)Section);
}

BOOLEAN WINAPI
TryAcquireSRWLockExclusive(PSRWLOCK Lock)
{
    return RtlTryAcquireSRWLockExclusive((PRTL_SRWLOCK)Lock);
}

VOID WINAPI
InitializeCriticalSection(LPCRITICAL_SECTION Section)
{
    RtlInitializeCriticalSection((PRTL_CRITICAL_SECTION)Section);
}

VOID WINAPI
DeleteCriticalSection(LPCRITICAL_SECTION Section)
{
    RtlDeleteCriticalSection((PRTL_CRITICAL_SECTION)Section);
}

VOID WINAPI
EnterCriticalSection(LPCRITICAL_SECTION Section)
{
    RtlEnterCriticalSection((PRTL_CRITICAL_SECTION)Section);
}

VOID WINAPI
LeaveCriticalSection(LPCRITICAL_SECTION Section)
{
    RtlLeaveCriticalSection((PRTL_CRITICAL_SECTION)Section);
}

VOID WINAPI
InitializeSRWLock(PSRWLOCK Lock)
{
    RtlInitializeSRWLock((PRTL_SRWLOCK)Lock);
}

VOID WINAPI
AcquireSRWLockExclusive(PSRWLOCK Lock)
{
    RtlAcquireSRWLockExclusive((PRTL_SRWLOCK)Lock);
}

VOID WINAPI
AcquireSRWLockShared(PSRWLOCK Lock)
{
    RtlAcquireSRWLockShared((PRTL_SRWLOCK)Lock);
}

VOID WINAPI
ReleaseSRWLockExclusive(PSRWLOCK Lock)
{
    RtlReleaseSRWLockExclusive((PRTL_SRWLOCK)Lock);
}

VOID WINAPI
ReleaseSRWLockShared(PSRWLOCK Lock)
{
    RtlReleaseSRWLockShared((PRTL_SRWLOCK)Lock);
}

VOID WINAPI
InitializeConditionVariable(PCONDITION_VARIABLE Condition)
{
    RtlInitializeConditionVariable((PRTL_CONDITION_VARIABLE)Condition);
}

VOID WINAPI
WakeConditionVariable(PCONDITION_VARIABLE Condition)
{
    RtlWakeConditionVariable((PRTL_CONDITION_VARIABLE)Condition);
}

VOID WINAPI
WakeAllConditionVariable(PCONDITION_VARIABLE Condition)
{
    RtlWakeAllConditionVariable((PRTL_CONDITION_VARIABLE)Condition);
}

static
BOOL
HostRtWaitResult(
    _In_ NTSTATUS Status)
{
    if (Status == STATUS_TIMEOUT)
    {
        NtCurrentTeb()->LastErrorValue = ERROR_TIMEOUT;
        return FALSE;
    }
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI
SleepConditionVariableCS(PCONDITION_VARIABLE Condition, PCRITICAL_SECTION Section, DWORD Milliseconds)
{
    LARGE_INTEGER Timeout;

    return HostRtWaitResult(RtlSleepConditionVariableCS((PRTL_CONDITION_VARIABLE)Condition,
                                                        (PRTL_CRITICAL_SECTION)Section,
                                                        HostRtTimeout(&Timeout, Milliseconds)));
}

BOOL WINAPI
SleepConditionVariableSRW(PCONDITION_VARIABLE Condition, PSRWLOCK Lock, DWORD Milliseconds, ULONG Flags)
{
    LARGE_INTEGER Timeout;

    return HostRtWaitResult(RtlSleepConditionVariableSRW((PRTL_CONDITION_VARIABLE)Condition,
                                                         (PRTL_SRWLOCK)Lock,
                                                         HostRtTimeout(&Timeout, Milliseconds),
                                                         Flags));
}

BOOL WINAPI
InitOnceExecuteOnce(PINIT_ONCE InitOnce, PINIT_ONCE_FN Function, PVOID Parameter, LPVOID *Context)
{
    return NT_SUCCESS(RtlRunOnceExecuteOnce((PRTL_RUN_ONCE)InitOnce, (PRTL_RUN_ONCE_INIT_FN)Function,
                                            Parameter, Context));
}

BOOL WINAPI
WaitOnAddress(volatile VOID *Address, PVOID Compare, SIZE_T Size, DWORD Milliseconds)
{
    LARGE_INTEGER Timeout;

    return HostRtWaitResult(RtlWaitOnAddress((const void *)Address, Compare, Size, HostRtTimeout(&Timeout, Milliseconds)));
}

VOID WINAPI
WakeByAddressSingle(PVOID Address)
{
    RtlWakeAddressSingle(Address);
}

VOID WINAPI
WakeByAddressAll(PVOID Address)
{
    RtlWakeAddressAll(Address);
}

HANDLE WINAPI
CreateEventW(LPSECURITY_ATTRIBUTES Attributes, BOOL ManualReset, BOOL InitialState, LPCWSTR Name)
{
    HANDLE Event;
    NTSTATUS Status;

    if (Name)
    {
        NtCurrentTeb()->LastErrorValue = ERROR_NOT_SUPPORTED;
        return NULL;
    }
    Status = NtCreateEvent(&Event, EVENT_ALL_ACCESS, NULL, ManualReset ? NotificationEvent : SynchronizationEvent,
                           (BOOLEAN)InitialState);
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return NULL;
    }
    return Event;
}

BOOL WINAPI
SetEvent(HANDLE Event)
{
    NTSTATUS Status = NtSetEvent(Event, NULL);

    if (!NT_SUCCESS(Status)) HostRtSetStatus(Status);
    return NT_SUCCESS(Status);
}

BOOL WINAPI
ResetEvent(HANDLE Event)
{
    NTSTATUS Status = NtClearEvent(Event);

    if (!NT_SUCCESS(Status)) HostRtSetStatus(Status);
    return NT_SUCCESS(Status);
}

BOOL WINAPI
CloseHandle(HANDLE Object)
{
    NTSTATUS Status = NtClose(Object);

    if (!NT_SUCCESS(Status)) HostRtSetStatus(Status);
    return NT_SUCCESS(Status);
}

BOOL WINAPI
DuplicateHandle(HANDLE SourceProcess, HANDLE Source, HANDLE TargetProcess, LPHANDLE Target, DWORD Access,
                BOOL Inherit, DWORD Options)
{
    NTSTATUS Status = NtDuplicateObject(SourceProcess, Source, TargetProcess, Target, Access,
                                        Inherit ? OBJ_INHERIT : 0, Options);

    if (!NT_SUCCESS(Status)) HostRtSetStatus(Status);
    return NT_SUCCESS(Status);
}

static
DWORD
HostRtWaitStatus(
    _In_ NTSTATUS Status)
{
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return WAIT_FAILED;
    }
    return (DWORD)Status;
}

DWORD WINAPI
WaitForSingleObject(HANDLE Object, DWORD Milliseconds)
{
    LARGE_INTEGER Timeout;

    return HostRtWaitStatus(NtWaitForSingleObject(Object, FALSE, HostRtTimeout(&Timeout, Milliseconds)));
}

DWORD WINAPI
WaitForSingleObjectEx(HANDLE Object, DWORD Milliseconds, BOOL Alertable)
{
    LARGE_INTEGER Timeout;

    return HostRtWaitStatus(NtWaitForSingleObject(Object, (BOOLEAN)Alertable, HostRtTimeout(&Timeout, Milliseconds)));
}

DWORD WINAPI
WaitForMultipleObjects(DWORD Count, CONST HANDLE *Objects, BOOL WaitAll, DWORD Milliseconds)
{
    LARGE_INTEGER Timeout;

    return HostRtWaitStatus(NtWaitForMultipleObjects(Count, (PHANDLE)Objects, WaitAll ? WaitAll : WaitAny,
                                                     FALSE, HostRtTimeout(&Timeout, Milliseconds)));
}

VOID WINAPI
Sleep(DWORD Milliseconds)
{
    LARGE_INTEGER Timeout;

    NtDelayExecution(FALSE, HostRtTimeout(&Timeout, Milliseconds));
}


VOID WINAPI
GetSystemInfo(LPSYSTEM_INFO Information)
{
    SYSTEM_BASIC_INFORMATION Basic;
    SYSTEM_PROCESSOR_INFORMATION Processor;

    RtlZeroMemory(Information, sizeof(*Information));
    if (!NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL)) ||
        !NT_SUCCESS(NtQuerySystemInformation(SystemProcessorInformation, &Processor, sizeof(Processor), NULL)))
    {
        return;
    }
    Information->wProcessorArchitecture = Processor.ProcessorArchitecture;
    Information->wProcessorLevel = Processor.ProcessorLevel;
    Information->wProcessorRevision = Processor.ProcessorRevision;
    Information->dwPageSize = Basic.PageSize;
    Information->lpMinimumApplicationAddress = (PVOID)Basic.MinimumUserModeAddress;
    Information->lpMaximumApplicationAddress = (PVOID)Basic.MaximumUserModeAddress;
    Information->dwActiveProcessorMask = Basic.ActiveProcessorsAffinityMask;
    Information->dwNumberOfProcessors = Basic.NumberOfProcessors;
    Information->dwAllocationGranularity = Basic.AllocationGranularity;
}

BOOL WINAPI
GlobalMemoryStatusEx(LPMEMORYSTATUSEX Status)
{
    SYSTEM_BASIC_INFORMATION Basic;
    SYSTEM_PERFORMANCE_INFORMATION Performance;
    NTSTATUS QueryStatus;

    if (Status->dwLength != sizeof(*Status))
    {
        NtCurrentTeb()->LastErrorValue = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    QueryStatus = NtQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL);
    if (NT_SUCCESS(QueryStatus))
        QueryStatus = NtQuerySystemInformation(SystemPerformanceInformation, &Performance, sizeof(Performance), NULL);
    if (!NT_SUCCESS(QueryStatus))
    {
        HostRtSetStatus(QueryStatus);
        return FALSE;
    }
    Status->ullTotalPhys = (ULONGLONG)Basic.NumberOfPhysicalPages * Basic.PageSize;
    Status->ullAvailPhys = (ULONGLONG)Performance.AvailablePages * Basic.PageSize;
    Status->dwMemoryLoad = Status->ullTotalPhys ?
        (DWORD)(100 - Status->ullAvailPhys * 100 / Status->ullTotalPhys) : 0;
    Status->ullTotalPageFile = (ULONGLONG)Performance.CommitLimit * Basic.PageSize;
    Status->ullAvailPageFile = (ULONGLONG)(Performance.CommitLimit - Performance.CommittedPages) * Basic.PageSize;
    Status->ullTotalVirtual = Basic.MaximumUserModeAddress - Basic.MinimumUserModeAddress + 1;
    Status->ullAvailVirtual = Status->ullTotalVirtual;
    Status->ullAvailExtendedVirtual = 0;
    return TRUE;
}

VOID WINAPI
GetSystemTimeAsFileTime(LPFILETIME Time)
{
    LARGE_INTEGER SystemTime;

    NtQuerySystemTime(&SystemTime);
    Time->dwLowDateTime = SystemTime.LowPart;
    Time->dwHighDateTime = SystemTime.HighPart;
}

DWORD WINAPI
GetTickCount(VOID)
{
    return NtGetTickCount();
}

VOID WINAPI
GetSystemTimePreciseAsFileTime(LPFILETIME Time)
{
    LARGE_INTEGER SystemTime;

    SystemTime.QuadPart = RtlGetSystemTimePrecise();
    Time->dwLowDateTime = SystemTime.LowPart;
    Time->dwHighDateTime = SystemTime.HighPart;
}

DWORD WINAPI
GetActiveProcessorCount(WORD Group)
{
    SYSTEM_BASIC_INFORMATION Basic;

    if (Group && Group != ALL_PROCESSOR_GROUPS) return 0;
    if (!NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &Basic, sizeof(Basic), NULL))) return 0;
    return Basic.NumberOfProcessors;
}

BOOL WINAPI
QueryPerformanceCounter(LARGE_INTEGER *Counter)
{
    return RtlQueryPerformanceCounter(Counter);
}

BOOL WINAPI
QueryPerformanceFrequency(LARGE_INTEGER *Frequency)
{
    return RtlQueryPerformanceFrequency(Frequency);
}


BOOL WINAPI
VirtualProtect(LPVOID Address, SIZE_T Size, DWORD Protect, PDWORD OldProtect)
{
    NTSTATUS Status = NtProtectVirtualMemory(NtCurrentProcess(), &Address, &Size, Protect, OldProtect);

    if (!NT_SUCCESS(Status)) HostRtSetStatus(Status);
    return NT_SUCCESS(Status);
}

SIZE_T WINAPI
VirtualQuery(LPCVOID Address, PMEMORY_BASIC_INFORMATION Information, SIZE_T Length)
{
    SIZE_T Returned = 0;
    NTSTATUS Status = NtQueryVirtualMemory(NtCurrentProcess(), (PVOID)Address, MemoryBasicInformation,
                                           Information, Length, &Returned);

    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return 0;
    }
    return Returned;
}


static
ULONG64
NTAPI
HostRtWglCreateContext(
    _In_ HDC Dc)
{
    static ULONG64 Cache;
    const ULONG64 Arguments[] = { (ULONG64)Dc };

    return HostRtCallGuest("opengl32.dll", "wglCreateContext", &Cache, "qq", Arguments);
}

static
ULONG64
NTAPI
HostRtWglDeleteContext(
    _In_ HGLRC Context)
{
    static ULONG64 Cache;
    const ULONG64 Arguments[] = { (ULONG64)Context };

    return HostRtCallGuest("opengl32.dll", "wglDeleteContext", &Cache, "wq", Arguments);
}

static
BOOLEAN
HostRtIsGuestOpenGl(
    _In_ LPCSTR Name)
{
    return !_stricmp(Name, "opengl32.dll") || !_stricmp(Name, "opengl32");
}

HMODULE WINAPI
GetModuleHandleA(LPCSTR Name)
{
    ANSI_STRING NameAnsi;
    UNICODE_STRING NameUnicode;
    PVOID Base;
    NTSTATUS Status;

    if (!Name) return (HMODULE)NtCurrentPeb()->ImageBaseAddress;
    if (HostRtIsGuestOpenGl(Name)) return (HMODULE)&HostRtGuestOpenGl;

    RtlInitAnsiString(&NameAnsi, Name);
    Status = RtlAnsiStringToUnicodeString(&NameUnicode, &NameAnsi, TRUE);
    if (NT_SUCCESS(Status))
    {
        Status = LdrGetDllHandle(NULL, NULL, &NameUnicode, &Base);
        RtlFreeUnicodeString(&NameUnicode);
    }
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return NULL;
    }
    return (HMODULE)Base;
}

FARPROC WINAPI
GetProcAddress(HMODULE Module, LPCSTR Name)
{
    ANSI_STRING NameAnsi;
    PVOID Address;
    NTSTATUS Status;

    if (Module == (HMODULE)&HostRtGuestOpenGl)
    {
        if ((ULONG_PTR)Name > MAXUSHORT && !strcmp(Name, "wglCreateContext")) return (FARPROC)HostRtWglCreateContext;
        if ((ULONG_PTR)Name > MAXUSHORT && !strcmp(Name, "wglDeleteContext")) return (FARPROC)HostRtWglDeleteContext;
        NtCurrentTeb()->LastErrorValue = ERROR_PROC_NOT_FOUND;
        return NULL;
    }

    if ((ULONG_PTR)Name <= MAXUSHORT)
    {
        Status = LdrGetProcedureAddress(Module, NULL, (ULONG)(ULONG_PTR)Name, &Address);
    }
    else
    {
        RtlInitAnsiString(&NameAnsi, Name);
        Status = LdrGetProcedureAddress(Module, &NameAnsi, 0, &Address);
    }
    if (!NT_SUCCESS(Status))
    {
        HostRtSetStatus(Status);
        return NULL;
    }
    return (FARPROC)Address;
}

HMODULE WINAPI
LoadLibraryA(LPCSTR Name)
{
    NtCurrentTeb()->LastErrorValue = ERROR_MOD_NOT_FOUND;
    return NULL;
}

HMODULE WINAPI
LoadLibraryW(LPCWSTR Name)
{
    NtCurrentTeb()->LastErrorValue = ERROR_MOD_NOT_FOUND;
    return NULL;
}

BOOL WINAPI
FreeLibrary(HMODULE Module)
{
    return TRUE;
}

static
PCUNICODE_STRING
HostRtModulePath(
    _In_opt_ HMODULE Module,
    _Out_ PULONG_PTR Cookie)
{
    PLDR_DATA_TABLE_ENTRY Entry;
    PLIST_ENTRY Head, Link;

    *Cookie = 0;
    if (!Module) return &NtCurrentPeb()->ProcessParameters->ImagePathName;

    LdrLockLoaderLock(0, NULL, Cookie);
    Head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList;
    for (Link = Head->Flink; Link != Head; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        if (Entry->DllBase == Module) return &Entry->FullDllName;
    }
    return NULL;
}

DWORD WINAPI
GetModuleFileNameW(HMODULE Module, LPWSTR Buffer, DWORD Size)
{
    PCUNICODE_STRING Path;
    ULONG_PTR Cookie;
    DWORD Length = 0;

    Path = HostRtModulePath(Module, &Cookie);
    if (Path && Size)
    {
        Length = min((DWORD)(Path->Length / sizeof(WCHAR)), Size - 1);
        RtlCopyMemory(Buffer, Path->Buffer, Length * sizeof(WCHAR));
        Buffer[Length] = UNICODE_NULL;
        if (Length < Path->Length / sizeof(WCHAR))
        {
            NtCurrentTeb()->LastErrorValue = ERROR_INSUFFICIENT_BUFFER;
            Length = Size;
        }
    }
    else
    {
        NtCurrentTeb()->LastErrorValue = Path ? ERROR_INSUFFICIENT_BUFFER : ERROR_MOD_NOT_FOUND;
    }
    if (Cookie) LdrUnlockLoaderLock(0, Cookie);
    return Length;
}

BOOL WINAPI
GetModuleHandleExW(DWORD Flags, LPCWSTR Name, HMODULE *Module)
{
    PVOID Base = NULL;

    if (!Module || !(Flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) || !RtlPcToFileHeader((PVOID)Name, &Base))
    {
        if (Module) *Module = NULL;
        NtCurrentTeb()->LastErrorValue = ERROR_MOD_NOT_FOUND;
        return FALSE;
    }
    *Module = (HMODULE)Base;
    return TRUE;
}

DWORD WINAPI
GetFullPathNameW(LPCWSTR Name, DWORD Size, LPWSTR Buffer, LPWSTR *FilePart)
{
    return RtlGetFullPathName_U(Name, Size * sizeof(WCHAR), Buffer, FilePart) / sizeof(WCHAR);
}

int WINAPI
WideCharToMultiByte(UINT CodePage, DWORD Flags, LPCWSTR Source, int SourceLength, LPSTR Destination,
                    int DestinationLength, LPCSTR Default, LPBOOL UsedDefault)
{
    ULONG Length, Written = 0;
    NTSTATUS Status;

    if (SourceLength < 0) SourceLength = (int)wcslen(Source) + 1;
    Length = SourceLength * sizeof(WCHAR);
    if (UsedDefault) *UsedDefault = FALSE;
    if (CodePage == CP_UTF8)
        Status = RtlUnicodeToUTF8N(Destination, DestinationLength, &Written, Source, Length);
    else if (DestinationLength)
        Status = RtlUnicodeToMultiByteN(Destination, DestinationLength, &Written, (PWCH)Source, Length);
    else
        Status = RtlUnicodeToMultiByteSize(&Written, (PWCH)Source, Length);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL)
    {
        HostRtSetStatus(Status);
        return 0;
    }
    return (int)Written;
}

DWORD WINAPI
GetModuleFileNameA(HMODULE Module, LPSTR Buffer, DWORD Size)
{
    PCUNICODE_STRING Path = NULL;
    PLDR_DATA_TABLE_ENTRY Entry;
    ANSI_STRING PathAnsi;
    PLIST_ENTRY Head, Link;
    ULONG_PTR Cookie;
    DWORD Length;

    if (!Module)
    {
        Path = &NtCurrentPeb()->ProcessParameters->ImagePathName;
    }
    else
    {
        LdrLockLoaderLock(0, NULL, &Cookie);
        Head = &NtCurrentPeb()->Ldr->InLoadOrderModuleList;
        for (Link = Head->Flink; Link != Head; Link = Link->Flink)
        {
            Entry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            if (Entry->DllBase == Module)
            {
                Path = &Entry->FullDllName;
                break;
            }
        }
        LdrUnlockLoaderLock(0, Cookie);
    }

    if (!Path || !NT_SUCCESS(RtlUnicodeStringToAnsiString(&PathAnsi, Path, TRUE)))
    {
        NtCurrentTeb()->LastErrorValue = ERROR_MOD_NOT_FOUND;
        return 0;
    }
    if (!Size)
    {
        RtlFreeAnsiString(&PathAnsi);
        NtCurrentTeb()->LastErrorValue = ERROR_INSUFFICIENT_BUFFER;
        return 0;
    }
    Length = min((DWORD)PathAnsi.Length, Size - 1);
    memcpy(Buffer, PathAnsi.Buffer, Length);
    Buffer[Length] = 0;
    RtlFreeAnsiString(&PathAnsi);
    if (Length < PathAnsi.Length)
    {
        NtCurrentTeb()->LastErrorValue = ERROR_INSUFFICIENT_BUFFER;
        return Size;
    }
    return Length;
}

DWORD WINAPI
GetTempPathA(DWORD Size, LPSTR Buffer)
{
    NtCurrentTeb()->LastErrorValue = ERROR_PATH_NOT_FOUND;
    return 0;
}

UINT WINAPI
GetTempFileNameA(LPCSTR Path, LPCSTR Prefix, UINT Unique, LPSTR Name)
{
    NtCurrentTeb()->LastErrorValue = ERROR_PATH_NOT_FOUND;
    return 0;
}

HWND WINAPI
GetConsoleWindow(VOID)
{
    return NULL;
}


static
BOOLEAN
HostRtIsConsole(
    _In_ FILE *File)
{
    return File == &_iob[1] || File == &_iob[2];
}

void * __cdecl
__acrt_iob_func(unsigned Index)
{
    return &_iob[Index < RTL_NUMBER_OF(_iob) ? Index : 2];
}

static
int
HostRtPrint(
    _In_ FILE *File,
    _In_ const char *Format,
    _In_ va_list Arguments)
{
    char Buffer[1024];
    int Length;

    Length = _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    Buffer[sizeof(Buffer) - 1] = 0;
    if (HostRtIsConsole(File)) DbgPrint("GLHOST: %s", Buffer);
    return Length;
}

int __cdecl
__stdio_common_vfprintf(unsigned __int64 Options, FILE *File, const char *Format, void *Locale,
                        va_list Arguments)
{
    return HostRtPrint(File, Format, Arguments);
}

int __cdecl
vfprintf(FILE *File, const char *Format, va_list Arguments)
{
    return HostRtPrint(File, Format, Arguments);
}

int __cdecl
fprintf(FILE *File, const char *Format, ...)
{
    va_list Arguments;
    int Length;

    va_start(Arguments, Format);
    Length = HostRtPrint(File, Format, Arguments);
    va_end(Arguments);
    return Length;
}

int __cdecl
__stdio_common_vsprintf(unsigned __int64 Options, char *Buffer, size_t Count, const char *Format, void *Locale,
                        va_list Arguments)
{
    va_list Copy;
    int Length;

    va_copy(Copy, Arguments);
    Length = _vscprintf(Format, Copy);
    va_end(Copy);
    if (Length < 0) return -1;
    if (Buffer && Count)
    {
        _vsnprintf(Buffer, Count, Format, Arguments);
        Buffer[min((size_t)Length, Count - 1)] = 0;
    }
    if (Options & HOSTRT_STANDARD_SNPRINTF) return Length;
    return Buffer && (size_t)Length < Count ? Length : -1;
}

int __cdecl
__stdio_common_vsscanf(unsigned __int64 Options, const char *Buffer, size_t Count, const char *Format,
                       void *Locale, va_list Arguments)
{
    return ChpeVsscanf(Buffer, Format, Arguments);
}

int __cdecl
__stdio_common_vswprintf(unsigned __int64 Options, wchar_t *Buffer, size_t Count, const wchar_t *Format,
                         void *Locale, va_list Arguments)
{
    va_list Copy;
    int Length;

    if (!Buffer || !Count)
    {
        va_copy(Copy, Arguments);
        Length = _vsnwprintf(NULL, 0, Format, Copy);
        va_end(Copy);
        return Length;
    }
    Length = _vsnwprintf(Buffer, Count, Format, Arguments);
    if (Length < 0 || (size_t)Length >= Count)
    {
        Buffer[Count - 1] = 0;
        return Options & HOSTRT_STANDARD_SNPRINTF ? Length : -1;
    }
    return Length;
}

size_t __cdecl
fwrite(const void *Buffer, size_t Size, size_t Count, FILE *File)
{
    if (!HostRtIsConsole(File)) return 0;
    DbgPrint("GLHOST: %.*s", (int)(Size * Count), (const char *)Buffer);
    return Count;
}

int __cdecl
fputs(const char *String, FILE *File)
{
    if (!HostRtIsConsole(File)) return HOSTRT_EOF;
    DbgPrint("GLHOST: %s", String);
    return 0;
}

int __cdecl
fputc(int Character, FILE *File)
{
    if (!HostRtIsConsole(File)) return HOSTRT_EOF;
    DbgPrint("%c", Character);
    return Character;
}

int __cdecl
putchar(int Character)
{
    return fputc(Character, &_iob[1]);
}

int __cdecl
puts(const char *String)
{
    DbgPrint("GLHOST: %s\n", String);
    return 0;
}

int __cdecl
fflush(FILE *File)
{
    return 0;
}

int __cdecl
ferror(FILE *File)
{
    return 0;
}

void __cdecl
clearerr(FILE *File)
{
}

int __cdecl
setvbuf(FILE *File, char *Buffer, int Mode, size_t Size)
{
    return 0;
}

FILE * __cdecl
fopen(const char *Path, const char *Mode)
{
    *_errno() = HOSTRT_ENOENT;
    return NULL;
}

FILE * __cdecl
_fdopen(int Descriptor, const char *Mode)
{
    *_errno() = HOSTRT_EBADF;
    return NULL;
}

int __cdecl
fclose(FILE *File)
{
    return HostRtIsConsole(File) ? 0 : HOSTRT_EOF;
}

size_t __cdecl
fread(void *Buffer, size_t Size, size_t Count, FILE *File)
{
    return 0;
}

int __cdecl
getc(FILE *File)
{
    return HOSTRT_EOF;
}

int __cdecl
fseek(FILE *File, long Offset, int Origin)
{
    *_errno() = HOSTRT_EBADF;
    return -1;
}

long __cdecl
ftell(FILE *File)
{
    *_errno() = HOSTRT_EBADF;
    return -1;
}

int __cdecl
_fileno(FILE *File)
{
    return File >= _iob && File < _iob + RTL_NUMBER_OF(_iob) ? (int)(File - _iob) : -1;
}

int __cdecl
_isatty(int Descriptor)
{
    return 0;
}

int __cdecl
isatty(int Descriptor)
{
    return _isatty(Descriptor);
}

int __cdecl
fileno(FILE *File)
{
    return _fileno(File);
}

int __cdecl
_open(const char *Path, int Flags, ...)
{
    *_errno() = HOSTRT_ENOENT;
    return -1;
}

int __cdecl
_read(int Descriptor, void *Buffer, unsigned Count)
{
    *_errno() = HOSTRT_EBADF;
    return -1;
}

int __cdecl
_close(int Descriptor)
{
    *_errno() = HOSTRT_EBADF;
    return -1;
}

int __cdecl
_dup(int Descriptor)
{
    *_errno() = HOSTRT_EBADF;
    return -1;
}

int __cdecl
_fstat64i32(int Descriptor, void *Status)
{
    *_errno() = HOSTRT_EBADF;
    return -1;
}

int __cdecl
_access(const char *Path, int Mode)
{
    *_errno() = HOSTRT_ENOENT;
    return -1;
}

int __cdecl
_unlink(const char *Path)
{
    *_errno() = HOSTRT_ENOENT;
    return -1;
}

int __cdecl
remove(const char *Path)
{
    return _unlink(Path);
}

int __cdecl
unlink(const char *Path)
{
    return _unlink(Path);
}

int __cdecl
access(const char *Path, int Mode)
{
    return _access(Path, Mode);
}

int __cdecl
close(int Descriptor)
{
    return _close(Descriptor);
}

int __cdecl
_mkdir(const char *Path)
{
    *_errno() = HOSTRT_ENOENT;
    return -1;
}

char * __cdecl
strdup(const char *String)
{
    return _strdup(String);
}

int __cdecl
stricmp(const char *Left, const char *Right)
{
    return _stricmp(Left, Right);
}

char * __cdecl
strerror(int Error)
{
    return "unknown error";
}


typedef struct _HOSTRT_BIG
{
    ULONG Count;
    ULONG Limb[HOSTRT_BIG_LIMBS];
} HOSTRT_BIG;

typedef struct _HOSTRT_FLOAT_FORMAT
{
    LONG Precision;
    LONG MinimumExponent;
    LONG MaximumExponent;
    LONG MaximumExactPower;
    ULONG64 Infinity;
    ULONG64 NotANumber;
    ULONG64 Sign;
} HOSTRT_FLOAT_FORMAT;

static const HOSTRT_FLOAT_FORMAT HostRtDoubleFormat =
{
    53, -1022, 1023, 22, 0x7FF0000000000000ULL, 0x7FF8000000000000ULL, 0x8000000000000000ULL
};
static const HOSTRT_FLOAT_FORMAT HostRtSingleFormat = { 24, -126, 127, 10, 0x7F800000, 0x7FC00000, 0x80000000 };

static
VOID
HostRtBigNormalize(
    _Inout_ HOSTRT_BIG *Big)
{
    while (Big->Count && !Big->Limb[Big->Count - 1]) Big->Count--;
}

static
BOOLEAN
HostRtBigMultiplyAdd(
    _Inout_ HOSTRT_BIG *Big,
    _In_ ULONG Factor,
    _In_ ULONG Addend)
{
    ULONG64 Carry = Addend;
    ULONG Index;

    for (Index = 0; Index < Big->Count; Index++)
    {
        Carry += (ULONG64)Big->Limb[Index] * Factor;
        Big->Limb[Index] = (ULONG)Carry;
        Carry >>= 32;
    }
    if (!Carry) return TRUE;
    if (Big->Count == HOSTRT_BIG_LIMBS) return FALSE;
    Big->Limb[Big->Count++] = (ULONG)Carry;
    return TRUE;
}

static
BOOLEAN
HostRtBigMultiplyPower5(
    _Inout_ HOSTRT_BIG *Big,
    _In_ ULONG Exponent)
{
    static const ULONG Powers[13] =
    {
        1, 5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125, 9765625, 48828125, 244140625
    };

    for (; Exponent >= 13; Exponent -= 13)
        if (!HostRtBigMultiplyAdd(Big, 1220703125, 0)) return FALSE;
    return HostRtBigMultiplyAdd(Big, Powers[Exponent], 0);
}

static
LONG
HostRtBigBitLength(
    _In_ const HOSTRT_BIG *Big)
{
    if (!Big->Count) return 0;
    return (LONG)(Big->Count * 32) - __builtin_clz(Big->Limb[Big->Count - 1]);
}

static
BOOLEAN
HostRtBigBit(
    _In_ const HOSTRT_BIG *Big,
    _In_ LONG Index)
{
    if (Index < 0 || (ULONG)Index / 32 >= Big->Count) return FALSE;
    return (Big->Limb[Index / 32] >> (Index % 32)) & 1;
}

static
BOOLEAN
HostRtBigAnyBelow(
    _In_ const HOSTRT_BIG *Big,
    _In_ LONG Index)
{
    LONG Word;

    if (Index <= 0) return FALSE;
    for (Word = 0; Word < Index / 32 && (ULONG)Word < Big->Count; Word++)
        if (Big->Limb[Word]) return TRUE;
    if (Index % 32 && (ULONG)Word < Big->Count && (Big->Limb[Word] & ((1UL << (Index % 32)) - 1))) return TRUE;
    return FALSE;
}

static
BOOLEAN
HostRtBigShiftLeft(
    _Inout_ HOSTRT_BIG *Big,
    _In_ LONG Shift)
{
    LONG Words = Shift / 32, Bits = Shift % 32, Index, Source;
    ULONG Count, High, Low;

    if (!Big->Count || !Shift) return TRUE;
    Count = (ULONG)((HostRtBigBitLength(Big) + Shift + 31) / 32);
    if (Count > HOSTRT_BIG_LIMBS) return FALSE;
    for (Index = (LONG)Count - 1; Index >= 0; Index--)
    {
        Source = Index - Words;
        High = Source >= 0 && (ULONG)Source < Big->Count ? Big->Limb[Source] : 0;
        Low = Source >= 1 && (ULONG)(Source - 1) < Big->Count ? Big->Limb[Source - 1] : 0;
        Big->Limb[Index] = Bits ? (High << Bits) | (Low >> (32 - Bits)) : High;
    }
    Big->Count = Count;
    HostRtBigNormalize(Big);
    return TRUE;
}

static
VOID
HostRtBigShiftRightOne(
    _Inout_ HOSTRT_BIG *Big)
{
    ULONG Index;

    for (Index = 0; Index < Big->Count; Index++)
        Big->Limb[Index] = (Big->Limb[Index] >> 1) | (Index + 1 < Big->Count ? Big->Limb[Index + 1] << 31 : 0);
    HostRtBigNormalize(Big);
}

static
INT
HostRtBigCompare(
    _In_ const HOSTRT_BIG *Left,
    _In_ const HOSTRT_BIG *Right)
{
    ULONG Index;

    if (Left->Count != Right->Count) return Left->Count < Right->Count ? -1 : 1;
    for (Index = Left->Count; Index-- > 0;)
        if (Left->Limb[Index] != Right->Limb[Index]) return Left->Limb[Index] < Right->Limb[Index] ? -1 : 1;
    return 0;
}

static
VOID
HostRtBigSubtract(
    _Inout_ HOSTRT_BIG *Left,
    _In_ const HOSTRT_BIG *Right)
{
    ULONG64 Borrow = 0, Value;
    ULONG Index;

    for (Index = 0; Index < Left->Count; Index++)
    {
        Value = (ULONG64)Left->Limb[Index] - (Index < Right->Count ? Right->Limb[Index] : 0) - Borrow;
        Left->Limb[Index] = (ULONG)Value;
        Borrow = Value >> 63;
    }
    HostRtBigNormalize(Left);
}

static
BOOLEAN
HostRtBigDivide(
    _Inout_ HOSTRT_BIG *Remainder,
    _In_ const HOSTRT_BIG *Divisor,
    _Out_ HOSTRT_BIG *Quotient)
{
    HOSTRT_BIG Shifted = *Divisor;
    LONG Shift = HostRtBigBitLength(Remainder) - HostRtBigBitLength(Divisor);

    RtlZeroMemory(Quotient, sizeof(*Quotient));
    if (Shift < 0) return TRUE;
    if (!HostRtBigShiftLeft(&Shifted, Shift)) return FALSE;
    Quotient->Count = (ULONG)Shift / 32 + 1;
    for (; Shift >= 0; Shift--)
    {
        if (HostRtBigCompare(Remainder, &Shifted) >= 0)
        {
            HostRtBigSubtract(Remainder, &Shifted);
            Quotient->Limb[Shift / 32] |= 1UL << (Shift % 32);
        }
        HostRtBigShiftRightOne(&Shifted);
    }
    HostRtBigNormalize(Quotient);
    return TRUE;
}

static
ULONG64
HostRtRoundFloat(
    _In_ const HOSTRT_BIG *Mantissa,
    _In_ LONG Exponent,
    _In_ BOOLEAN Sticky,
    _In_ const HOSTRT_FLOAT_FORMAT *Format,
    _Inout_ BOOLEAN *Range)
{
    LONG Length = HostRtBigBitLength(Mantissa);
    LONG Top = Length - 1 + Exponent;
    LONG Precision = Format->Precision;
    LONG Index;
    ULONG64 Kept = 0;

    if (Top > Format->MaximumExponent)
    {
        *Range = TRUE;
        return Format->Infinity;
    }
    if (Top < Format->MinimumExponent) Precision -= Format->MinimumExponent - Top;
    if (Precision < 0)
    {
        *Range = TRUE;
        return 0;
    }
    for (Index = Length - 1; Index >= Length - Precision; Index--)
        Kept = (Kept << 1) | HostRtBigBit(Mantissa, Index);
    if (HostRtBigBit(Mantissa, Length - Precision - 1) &&
        (Sticky || HostRtBigAnyBelow(Mantissa, Length - Precision - 1) || (Kept & 1)))
    {
        Kept++;
    }
    if (Precision < Format->Precision)
    {
        if (!Kept) *Range = TRUE;
        return Kept;
    }
    if (Kept >> Format->Precision)
    {
        Kept >>= 1;
        if (++Top > Format->MaximumExponent)
        {
            *Range = TRUE;
            return Format->Infinity;
        }
    }
    return ((ULONG64)(Top + Format->MaximumExponent) << (Format->Precision - 1)) |
           (Kept & ((1ULL << (Format->Precision - 1)) - 1));
}

static
LONG
HostRtParseExponent(
    _Inout_ const char **Cursor,
    _In_ char Marker)
{
    const char *Mark = *Cursor;
    BOOLEAN Negative = FALSE;
    LONG Value = 0;

    if ((*Mark | 0x20) != Marker) return 0;
    Mark++;
    if (*Mark == '+' || *Mark == '-') Negative = *Mark++ == '-';
    if (*Mark < '0' || *Mark > '9') return 0;
    for (; *Mark >= '0' && *Mark <= '9'; Mark++)
        if (Value < 100000) Value = Value * 10 + (*Mark - '0');
    *Cursor = Mark;
    return Negative ? -Value : Value;
}

static
LONG
HostRtHexDigit(
    _In_ char Character)
{
    if (Character >= '0' && Character <= '9') return Character - '0';
    if ((Character | 0x20) >= 'a' && (Character | 0x20) <= 'f') return (Character | 0x20) - 'a' + 10;
    return -1;
}

static
ULONG64
HostRtParseFloat(
    _In_ const char *String,
    _Out_opt_ char **End,
    _In_ const HOSTRT_FLOAT_FORMAT *Format)
{
    static const ULONG Powers10[10] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000 };
    static const double ExactDouble[23] =
    {
        1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18,
        1e19, 1e20, 1e21, 1e22
    };
    static const float ExactSingle[11] = { 1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f };
    const char *Cursor = String, *Mark;
    HOSTRT_BIG Digits = { 0 }, Divisor = { 1, { 1 } }, Quotient;
    BOOLEAN Negative = FALSE, Point = FALSE, Any = FALSE, Started = FALSE, Sticky = FALSE, Range = FALSE;
    LONG Scale = 0, Stored = 0, Decimal, Shift, Digit;
    ULONG Chunk = 0, ChunkDigits = 0;
    ULONG64 Small = 0, Bits = 0;
    double DoubleValue;
    float SingleValue;

    while (*Cursor == ' ' || (*Cursor >= '\t' && *Cursor <= '\r')) Cursor++;
    if (*Cursor == '+' || *Cursor == '-') Negative = *Cursor++ == '-';

    if (!_strnicmp(Cursor, "inf", 3))
    {
        Cursor += _strnicmp(Cursor + 3, "inity", 5) ? 3 : 8;
        Bits = Format->Infinity;
        goto Done;
    }
    if (!_strnicmp(Cursor, "nan", 3))
    {
        Cursor += 3;
        if (*Cursor == '(')
        {
            for (Mark = Cursor + 1; (*Mark >= '0' && *Mark <= '9') || ((*Mark | 0x20) >= 'a' && (*Mark | 0x20) <= 'z') ||
                                    *Mark == '_'; Mark++);
            if (*Mark == ')') Cursor = Mark + 1;
        }
        Bits = Format->NotANumber;
        goto Done;
    }

    if (Cursor[0] == '0' && (Cursor[1] | 0x20) == 'x')
    {
        Mark = Cursor + 2;
        for (;; Mark++)
        {
            if (*Mark == '.' && !Point)
            {
                Point = TRUE;
                continue;
            }
            Digit = HostRtHexDigit(*Mark);
            if (Digit < 0) break;
            Any = TRUE;
            if (!Started && !Digit)
            {
                if (Point) Scale -= 4;
                continue;
            }
            Started = TRUE;
            if (Stored < 16)
            {
                Small = (Small << 4) | (ULONG)Digit;
                Stored++;
                if (Point) Scale -= 4;
            }
            else
            {
                if (Digit) Sticky = TRUE;
                if (!Point) Scale += 4;
            }
        }
        if (!Any)
        {
            Cursor++;
            goto Done;
        }
        Cursor = Mark;
        Scale += HostRtParseExponent(&Cursor, 'p');
        if (!Started) goto Done;
        Digits.Limb[0] = (ULONG)Small;
        Digits.Limb[1] = (ULONG)(Small >> 32);
        Digits.Count = 2;
        HostRtBigNormalize(&Digits);
        Bits = HostRtRoundFloat(&Digits, Scale, Sticky, Format, &Range);
        goto Done;
    }

    for (Mark = Cursor;; Mark++)
    {
        if (*Mark == '.' && !Point)
        {
            Point = TRUE;
            continue;
        }
        if (*Mark < '0' || *Mark > '9') break;
        Any = TRUE;
        Digit = *Mark - '0';
        if (!Started && !Digit)
        {
            if (Point) Scale--;
            continue;
        }
        Started = TRUE;
        if (Stored < HOSTRT_FLOAT_DIGITS)
        {
            Chunk = Chunk * 10 + (ULONG)Digit;
            if (++ChunkDigits == 9)
            {
                HostRtBigMultiplyAdd(&Digits, Powers10[9], Chunk);
                Chunk = ChunkDigits = 0;
            }
            if (Stored < 19) Small = Small * 10 + (ULONG)Digit;
            Stored++;
            if (Point) Scale--;
        }
        else
        {
            if (Digit) Sticky = TRUE;
            if (!Point) Scale++;
        }
    }
    if (!Any)
    {
        Cursor = String;
        Negative = FALSE;
        goto Done;
    }
    Cursor = Mark;
    Decimal = Scale + HostRtParseExponent(&Cursor, 'e');
    if (!Started) goto Done;
    if (ChunkDigits)
    {
        HostRtBigMultiplyAdd(&Digits, Powers10[ChunkDigits], Chunk);
    }

    if (Stored + Decimal > 310)
    {
        Range = TRUE;
        Bits = Format->Infinity;
        goto Done;
    }
    if (Stored + Decimal < -330)
    {
        Range = TRUE;
        goto Done;
    }

    if (Stored <= 19 && !Sticky && Small <= (1ULL << Format->Precision) &&
        Decimal >= -Format->MaximumExactPower && Decimal <= Format->MaximumExactPower)
    {
        if (Format == &HostRtDoubleFormat)
        {
            DoubleValue = (double)Small;
            DoubleValue = Decimal >= 0 ? DoubleValue * ExactDouble[Decimal] : DoubleValue / ExactDouble[-Decimal];
            RtlCopyMemory(&Bits, &DoubleValue, sizeof(DoubleValue));
        }
        else
        {
            SingleValue = (float)Small;
            SingleValue = Decimal >= 0 ? SingleValue * ExactSingle[Decimal] : SingleValue / ExactSingle[-Decimal];
            RtlCopyMemory(&Bits, &SingleValue, sizeof(SingleValue));
        }
        goto Done;
    }

    if (Decimal >= 0)
    {
        if (!HostRtBigMultiplyPower5(&Digits, (ULONG)Decimal))
        {
            Range = TRUE;
            Bits = Format->Infinity;
            goto Done;
        }
        Bits = HostRtRoundFloat(&Digits, Decimal, Sticky, Format, &Range);
        goto Done;
    }

    Shift = 0;
    if (HostRtBigMultiplyPower5(&Divisor, (ULONG)-Decimal))
    {
        Shift = HostRtBigBitLength(&Divisor) - HostRtBigBitLength(&Digits) + Format->Precision + 3;
        if (Shift < 0) Shift = 0;
    }
    if (!Divisor.Count || !HostRtBigShiftLeft(&Digits, Shift) || !HostRtBigDivide(&Digits, &Divisor, &Quotient))
    {
        Range = TRUE;
        goto Done;
    }
    Bits = HostRtRoundFloat(&Quotient, Decimal - Shift, Sticky || Digits.Count != 0, Format, &Range);

Done:
    if (Negative) Bits |= Format->Sign;
    if (Range) *_errno() = HOSTRT_ERANGE;
    if (End) *End = (char *)Cursor;
    return Bits;
}

double __cdecl
strtod(const char *String, char **End)
{
    ULONG64 Bits = HostRtParseFloat(String, End, &HostRtDoubleFormat);
    double Value;

    RtlCopyMemory(&Value, &Bits, sizeof(Value));
    return Value;
}

float __cdecl
strtof(const char *String, char **End)
{
    ULONG Bits = (ULONG)HostRtParseFloat(String, End, &HostRtSingleFormat);
    float Value;

    RtlCopyMemory(&Value, &Bits, sizeof(Value));
    return Value;
}

double __cdecl
atof(const char *String)
{
    return strtod(String, NULL);
}

long double __cdecl
strtold(const char *String, char **End)
{
    return strtod(String, End);
}

double __cdecl
wcstod(const wchar_t *String, wchar_t **End)
{
    char Buffer[128];
    char *Narrow;
    size_t Index;
    double Value;

    for (Index = 0; Index < sizeof(Buffer) - 1 && String[Index] && String[Index] < 0x80; Index++)
        Buffer[Index] = (char)String[Index];
    Buffer[Index] = 0;
    Value = strtod(Buffer, &Narrow);
    if (End) *End = (wchar_t *)String + (Narrow - Buffer);
    return Value;
}

float __cdecl
wcstof(const wchar_t *String, wchar_t **End)
{
    return (float)wcstod(String, End);
}

long double __cdecl
wcstold(const wchar_t *String, wchar_t **End)
{
    return wcstod(String, End);
}

__int64 __cdecl
wcstoll(const wchar_t *String, wchar_t **End, int Base)
{
    return _wcstoi64(String, End, Base);
}

unsigned __int64 __cdecl
wcstoull(const wchar_t *String, wchar_t **End, int Base)
{
    return _wcstoui64(String, End, Base);
}

__int64 __cdecl
strtoll(const char *String, char **End, int Base)
{
    return _strtoi64(String, End, Base);
}

char * __cdecl
strtok(char *String, const char *Delimiters)
{
    return strtok_s(String, Delimiters, (char **)HostRtSlot(&HostRtStrtokSlot));
}

static
VOID
HostRtSwap(
    _Inout_ PUCHAR Left,
    _Inout_ PUCHAR Right,
    _In_ size_t Width)
{
    UCHAR Byte;

    while (Width--)
    {
        Byte = *Left;
        *Left++ = *Right;
        *Right++ = Byte;
    }
}

static
VOID
HostRtSift(
    _Inout_ PUCHAR Base,
    _In_ size_t Start,
    _In_ size_t Count,
    _In_ size_t Width,
    _In_ HOSTRT_COMPARE_S Compare,
    _In_opt_ void *Context)
{
    size_t Root = Start, Child;

    while ((Child = Root * 2 + 1) < Count)
    {
        if (Child + 1 < Count && Compare(Context, Base + Child * Width, Base + (Child + 1) * Width) < 0) Child++;
        if (Compare(Context, Base + Root * Width, Base + Child * Width) >= 0) return;
        HostRtSwap(Base + Root * Width, Base + Child * Width, Width);
        Root = Child;
    }
}

void __cdecl
qsort_s(void *Base, size_t Count, size_t Width, HOSTRT_COMPARE_S Compare, void *Context)
{
    PUCHAR Bytes = Base;
    size_t Index;

    if (Count < 2 || !Width) return;
    for (Index = Count / 2; Index-- > 0;) HostRtSift(Bytes, Index, Count, Width, Compare, Context);
    for (Index = Count - 1; Index > 0; Index--)
    {
        HostRtSwap(Bytes, Bytes + Index * Width, Width);
        HostRtSift(Bytes, 0, Index, Width, Compare, Context);
    }
}

double __cdecl
log2(double Value)
{
    return log(Value) * 1.4426950408889634073599246810019;
}

float __cdecl
log2f(float Value)
{
    return (float)log2(Value);
}


#define HOSTRT_RESULT BOOL
BOOL WINAPI
AdjustWindowRectEx(LPRECT Rect, DWORD Style, BOOL Menu, DWORD ExStyle)
{
    HOSTRT_GUEST("user32.dll", "AdjustWindowRectEx", "wqwww", (ULONG64)Rect, Style, Menu, ExStyle);
}

BOOL WINAPI
ClientToScreen(HWND Window, LPPOINT Point)
{
    HOSTRT_GUEST("user32.dll", "ClientToScreen", "wqq", (ULONG64)Window, (ULONG64)Point);
}

BOOL WINAPI
DestroyWindow(HWND Window)
{
    HOSTRT_GUEST("user32.dll", "DestroyWindow", "wq", (ULONG64)Window);
}

BOOL WINAPI
EnumDisplaySettingsA(LPCSTR Device, DWORD Mode, LPDEVMODEA DevMode)
{
    HOSTRT_GUEST("user32.dll", "EnumDisplaySettingsA", "wqwq", (ULONG64)Device, Mode, (ULONG64)DevMode);
}

BOOL WINAPI
GetClientRect(HWND Window, LPRECT Rect)
{
    HOSTRT_GUEST("user32.dll", "GetClientRect", "wqq", (ULONG64)Window, (ULONG64)Rect);
}

BOOL WINAPI
GetWindowRect(HWND Window, LPRECT Rect)
{
    HOSTRT_GUEST("user32.dll", "GetWindowRect", "wqq", (ULONG64)Window, (ULONG64)Rect);
}

BOOL WINAPI
IsWindowVisible(HWND Window)
{
    HOSTRT_GUEST("user32.dll", "IsWindowVisible", "wq", (ULONG64)Window);
}

BOOL WINAPI
UnhookWindowsHookEx(HHOOK Hook)
{
    HOSTRT_GUEST("user32.dll", "UnhookWindowsHookEx", "wq", (ULONG64)Hook);
}

BOOL WINAPI
SetPixelFormat(HDC Dc, int Format, CONST PIXELFORMATDESCRIPTOR *Descriptor)
{
    HOSTRT_GUEST("gdi32.dll", "SetPixelFormat", "wqwq", (ULONG64)Dc, (ULONG64)(LONG64)Format, (ULONG64)Descriptor);
}
#undef HOSTRT_RESULT

#define HOSTRT_RESULT int
int WINAPI
ReleaseDC(HWND Window, HDC Dc)
{
    HOSTRT_GUEST("user32.dll", "ReleaseDC", "wqq", (ULONG64)Window, (ULONG64)Dc);
}

int WINAPI
DescribePixelFormat(HDC Dc, int Format, UINT Size, LPPIXELFORMATDESCRIPTOR Descriptor)
{
    HOSTRT_GUEST("gdi32.dll", "DescribePixelFormat", "wqwwq", (ULONG64)Dc, (ULONG64)(LONG64)Format, Size,
                 (ULONG64)Descriptor);
}

int WINAPI
GetPixelFormat(HDC Dc)
{
    HOSTRT_GUEST("gdi32.dll", "GetPixelFormat", "wq", (ULONG64)Dc);
}

int WINAPI
StretchDIBits(HDC Dc, int XDest, int YDest, int DestWidth, int DestHeight, int XSource, int YSource,
              int SourceWidth, int SourceHeight, CONST VOID *Bits, CONST BITMAPINFO *Info, UINT Usage, DWORD Rop)
{
    HOSTRT_GUEST("gdi32.dll", "StretchDIBits", "wqwwwwwwwwqqww", (ULONG64)Dc, (ULONG64)(LONG64)XDest,
                 (ULONG64)(LONG64)YDest, (ULONG64)(LONG64)DestWidth, (ULONG64)(LONG64)DestHeight,
                 (ULONG64)(LONG64)XSource, (ULONG64)(LONG64)YSource, (ULONG64)(LONG64)SourceWidth,
                 (ULONG64)(LONG64)SourceHeight, (ULONG64)Bits, (ULONG64)Info, Usage, Rop);
}
#undef HOSTRT_RESULT

#define HOSTRT_RESULT LRESULT
LRESULT WINAPI
CallNextHookEx(HHOOK Hook, int Code, WPARAM WParam, LPARAM LParam)
{
    HOSTRT_GUEST("user32.dll", "CallNextHookEx", "qqwqq", (ULONG64)Hook, (ULONG64)(LONG64)Code, WParam,
                 (ULONG64)LParam);
}

LRESULT WINAPI
DefWindowProcA(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    HOSTRT_GUEST("user32.dll", "DefWindowProcA", "qqwqq", (ULONG64)Window, Message, WParam, (ULONG64)LParam);
}
#undef HOSTRT_RESULT

#define HOSTRT_RESULT HANDLE
HWND WINAPI
CreateWindowExA(DWORD ExStyle, LPCSTR ClassName, LPCSTR WindowName, DWORD Style, int X, int Y, int Width,
                int Height, HWND Parent, HMENU Menu, HINSTANCE Instance, LPVOID Parameter)
{
    HOSTRT_GUEST("user32.dll", "CreateWindowExA", "qwqqwwwwwqqqq", ExStyle, (ULONG64)ClassName,
                 (ULONG64)WindowName, Style, (ULONG64)(LONG64)X, (ULONG64)(LONG64)Y, (ULONG64)(LONG64)Width,
                 (ULONG64)(LONG64)Height, (ULONG64)Parent, (ULONG64)Menu, (ULONG64)Instance, (ULONG64)Parameter);
}

HDC WINAPI
GetDC(HWND Window)
{
    HOSTRT_GUEST("user32.dll", "GetDC", "qq", (ULONG64)Window);
}

HWND WINAPI
WindowFromDC(HDC Dc)
{
    HOSTRT_GUEST("user32.dll", "WindowFromDC", "qq", (ULONG64)Dc);
}

HCURSOR WINAPI
LoadCursorA(HINSTANCE Instance, LPCSTR Name)
{
    HOSTRT_GUEST("user32.dll", "LoadCursorA", "qqq", (ULONG64)Instance, (ULONG64)Name);
}

HICON WINAPI
LoadIconA(HINSTANCE Instance, LPCSTR Name)
{
    HOSTRT_GUEST("user32.dll", "LoadIconA", "qqq", (ULONG64)Instance, (ULONG64)Name);
}
#undef HOSTRT_RESULT

HHOOK WINAPI
SetWindowsHookExA(int Hook, HOOKPROC Procedure, HINSTANCE Instance, DWORD ThreadId)
{
    static ULONG64 Cache;
    ULONG64 Arguments[4];

    Arguments[0] = (ULONG64)(LONG64)Hook;
    Arguments[1] = EmuHostGetInterface()->GuestThunk((PVOID)Procedure, "qwqq");
    Arguments[2] = (ULONG64)Instance;
    Arguments[3] = ThreadId;
    if (!Arguments[1])
    {
        NtCurrentTeb()->LastErrorValue = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    return (HHOOK)HostRtCallGuest("user32.dll", "SetWindowsHookExA", &Cache, "qwqqw", Arguments);
}

ATOM WINAPI
RegisterClassA(CONST WNDCLASSA *Class)
{
    static ULONG64 Cache;
    WNDCLASSA Guest = *Class;
    ULONG64 Arguments[1];

    Guest.lpfnWndProc = (WNDPROC)(ULONG_PTR)EmuHostGetInterface()->GuestThunk((PVOID)Class->lpfnWndProc, "qqwqq");
    if (!Guest.lpfnWndProc)
    {
        NtCurrentTeb()->LastErrorValue = ERROR_NOT_ENOUGH_MEMORY;
        return 0;
    }
    Arguments[0] = (ULONG64)&Guest;
    return (ATOM)HostRtCallGuest("user32.dll", "RegisterClassA", &Cache, "Hq", Arguments);
}

BOOL WINAPI
UnionRect(LPRECT Destination, CONST RECT *Left, CONST RECT *Right)
{
    BOOL LeftEmpty = Left->left >= Left->right || Left->top >= Left->bottom;
    BOOL RightEmpty = Right->left >= Right->right || Right->top >= Right->bottom;

    if (LeftEmpty && RightEmpty)
    {
        Destination->left = Destination->top = Destination->right = Destination->bottom = 0;
        return FALSE;
    }
    if (LeftEmpty) *Destination = *Right;
    else if (RightEmpty) *Destination = *Left;
    else
    {
        Destination->left = min(Left->left, Right->left);
        Destination->top = min(Left->top, Right->top);
        Destination->right = max(Left->right, Right->right);
        Destination->bottom = max(Left->bottom, Right->bottom);
    }
    return TRUE;
}
