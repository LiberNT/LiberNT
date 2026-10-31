/*
 * PROJECT:     LiberNT AMD64 emulation host
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private declarations of the NT front end
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WIN32_NO_STATUS
#define NTOS_MODE_USER
#include <windef.h>
#include <winbase.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <ndk/ntndk.h>
#define __WINE_WINNT_EXCEPTION_REGISTRATION_RECORD
#include <wine/winnt.h>
#include <pseh/pseh2.h>
#include <reactos/emulation.h>
#include <reactos/emuhost.h>
#include <reactos/chpe.h>
#include "cpu.h"

#define EMU_MAX_SERVICE_ARGUMENTS 32
#define EMU_ADDRESS_SPACE_SIZE    ((SIZE_T)1 << 47)

#define EMU_SEG_CODE 0x33
#define EMU_SEG_DATA 0x2B
#define EMU_SEG_TEB  0x53

typedef struct _AMD64_MACHINE_FRAME
{
    ULONG64 Rip;
    USHORT SegCs;
    USHORT Fill1[3];
    ULONG EFlags;
    ULONG Fill2;
    ULONG64 Rsp;
    USHORT SegSs;
    USHORT Fill3[3];
} AMD64_MACHINE_FRAME, *PAMD64_MACHINE_FRAME;

typedef struct _AMD64_CONTEXT_CHUNK
{
    LONG Offset;
    ULONG Length;
} AMD64_CONTEXT_CHUNK;

typedef struct _AMD64_CONTEXT_EX
{
    AMD64_CONTEXT_CHUNK All;
    AMD64_CONTEXT_CHUNK Legacy;
    AMD64_CONTEXT_CHUNK XState;
    ULONG64 Reserved;
} AMD64_CONTEXT_EX;

typedef struct DECLSPEC_ALIGN(16) _AMD64_EXCEPTION_STACK
{
    AMD64_CONTEXT Context;
    AMD64_CONTEXT_EX ContextEx;
    EXCEPTION_RECORD ExceptionRecord;
    ULONG64 Alignment;
    AMD64_MACHINE_FRAME MachineFrame;
} AMD64_EXCEPTION_STACK, *PAMD64_EXCEPTION_STACK;

typedef struct DECLSPEC_ALIGN(16) _AMD64_APC_FRAME
{
    AMD64_CONTEXT Context;
    AMD64_MACHINE_FRAME MachineFrame;
} AMD64_APC_FRAME, *PAMD64_APC_FRAME;

typedef struct _AMD64_UCALLOUT_FRAME
{
    ULONG64 P1Home;
    ULONG64 P2Home;
    ULONG64 P3Home;
    ULONG64 P4Home;
    ULONG64 Buffer;
    ULONG Length;
    ULONG ApiNumber;
    AMD64_MACHINE_FRAME MachineFrame;
} AMD64_UCALLOUT_FRAME, *PAMD64_UCALLOUT_FRAME;

C_ASSERT(sizeof(AMD64_CONTEXT) == 0x4D0);
C_ASSERT(FIELD_OFFSET(AMD64_CONTEXT, Rax) == 0x78);
C_ASSERT(FIELD_OFFSET(AMD64_CONTEXT, Rip) == 0xF8);
C_ASSERT(sizeof(AMD64_MACHINE_FRAME) == 0x28);
C_ASSERT(FIELD_OFFSET(AMD64_EXCEPTION_STACK, ExceptionRecord) == 0x4F0);
C_ASSERT(FIELD_OFFSET(AMD64_EXCEPTION_STACK, MachineFrame) == 0x590);
C_ASSERT(FIELD_OFFSET(AMD64_APC_FRAME, MachineFrame) == 0x4D0);
C_ASSERT(FIELD_OFFSET(AMD64_UCALLOUT_FRAME, Buffer) == 0x20);
C_ASSERT(FIELD_OFFSET(AMD64_UCALLOUT_FRAME, MachineFrame) == 0x30);
C_ASSERT(sizeof(AMD64_UCALLOUT_FRAME) == 0x58);

typedef IMAGE_AMD64_RUNTIME_FUNCTION_ENTRY EMU_AMD64_RUNTIME_FUNCTION, *PEMU_AMD64_RUNTIME_FUNCTION;

typedef struct _EMU_AMD64_CONTEXT_POINTERS
{
    PM128A FloatingContext[16];
    PULONG64 IntegerContext[16];
} EMU_AMD64_CONTEXT_POINTERS, *PEMU_AMD64_CONTEXT_POINTERS;

typedef struct _EMU_AMD64_DISPATCHER_CONTEXT
{
    ULONG64 ControlPc;
    ULONG64 ImageBase;
    PEMU_AMD64_RUNTIME_FUNCTION FunctionEntry;
    ULONG64 EstablisherFrame;
    ULONG64 TargetIp;
    AMD64_CONTEXT *ContextRecord;
    ULONG64 LanguageHandler;
    PVOID HandlerData;
    PVOID HistoryTable;
    ULONG ScopeIndex;
    ULONG Fill0;
} EMU_AMD64_DISPATCHER_CONTEXT, *PEMU_AMD64_DISPATCHER_CONTEXT;

typedef struct _EMU_AMD64_SCOPE_TABLE
{
    ULONG Count;
    struct
    {
        ULONG BeginAddress;
        ULONG EndAddress;
        ULONG HandlerAddress;
        ULONG JumpTarget;
    } ScopeRecord[1];
} EMU_AMD64_SCOPE_TABLE, *PEMU_AMD64_SCOPE_TABLE;

typedef struct _EMU_AMD64_JUMP_BUFFER
{
    ULONG64 Frame;
    ULONG64 Rbx;
    ULONG64 Rsp;
    ULONG64 Rbp;
    ULONG64 Rsi;
    ULONG64 Rdi;
    ULONG64 R12;
    ULONG64 R13;
    ULONG64 R14;
    ULONG64 R15;
    ULONG64 Rip;
    ULONG MxCsr;
    USHORT FpCsr;
    USHORT Spare;
    M128A Xmm[10];
} EMU_AMD64_JUMP_BUFFER, *PEMU_AMD64_JUMP_BUFFER;

C_ASSERT(FIELD_OFFSET(EMU_AMD64_JUMP_BUFFER, Xmm) == 0x60);
C_ASSERT(sizeof(EMU_AMD64_CONTEXT_POINTERS) == 0x100);
C_ASSERT(FIELD_OFFSET(EMU_AMD64_DISPATCHER_CONTEXT, ContextRecord) == 0x28);
C_ASSERT(sizeof(EMU_AMD64_DISPATCHER_CONTEXT) == 0x50);

#define EMU_QUEUE_USER_APC_FLAGS_SPECIAL_USER_APC 0x00000001

typedef struct _EMU_APC_RETURN
{
    struct _EMU_APC_RETURN *Previous;
    PAMD64_APC_FRAME Frame;
    PCONTEXT NativeContext;
    ULONG64 CoreFrame;
    ULONG HostDepth;
} EMU_APC_RETURN, *PEMU_APC_RETURN;

typedef struct _EMU_CALLBACK
{
    struct _EMU_CALLBACK *Previous;
    AMD64_CONTEXT Context;
    ULONG64 Frame;
    ULONG HostDepth;
} EMU_CALLBACK, *PEMU_CALLBACK;

#define EMU_JUMP_BUFFER_SIZE 26

typedef struct _EMU_X64_UNWIND
{
    ULONG64 TargetFrame;
    ULONG64 TargetIp;
    ULONG64 ReturnValue;
    PEXCEPTION_RECORD Record;
    BOOLEAN Active;
    BOOLEAN Consolidating;
    EXCEPTION_RECORD RecordCopy;
    EMU_AMD64_DISPATCHER_CONTEXT Dispatcher;
    AMD64_CONTEXT Context;
    AMD64_CONTEXT Unwound;
} EMU_X64_UNWIND, *PEMU_X64_UNWIND;

typedef struct _EMU_BOUNDARY
{
    struct _EMU_BOUNDARY *Previous;
    struct _EMU_THREAD *Thread;
    ULONG64 RunFrame;
    struct _EMU_GUEST_CALL *GuestCall;
    ULONG Service;
    PEMU_REGISTERS Registers;
    AMD64_CONTEXT *Context;
    PEMU_X64_UNWIND Unwind;
    ULONG64 EmulatorSp;
} EMU_BOUNDARY, *PEMU_BOUNDARY;

C_ASSERT(FIELD_OFFSET(EMU_BOUNDARY, RunFrame) == 16);
C_ASSERT(FIELD_OFFSET(EMU_BOUNDARY, EmulatorSp) == 64);

typedef struct _EMU_TEB_STACK
{
    PVOID Base;
    PVOID Limit;
    PVOID Deallocation;
} EMU_TEB_STACK, *PEMU_TEB_STACK;

typedef struct _EMU_GUEST_CALL
{
    struct _EMU_GUEST_CALL *Previous;
    ULONG64 Jump[EMU_JUMP_BUFFER_SIZE];
    ULONG64 Rsp;
    ULONG64 Rip;
    ULONG64 Frame;
    ULONG HostDepth;
    ULONG64 Result;
    ULONG64 FloatResult;
    ULONG64 RunFrame;
    PEMU_BOUNDARY Boundary;
    EMU_TEB_STACK NativeStack;
    AMD64_CONTEXT Entry;
} EMU_GUEST_CALL, *PEMU_GUEST_CALL;

typedef struct _EMU_STACK
{
    PVOID Base;
    PVOID Limit;
} EMU_STACK, *PEMU_STACK;

typedef struct _EMU_THREAD
{
    PEMULATION_THREAD Host;
    PEMU_CPU Cpu;
    PTEB GuestTeb;
    ULONG HostDepth;
    PEMU_CALLBACK Callback;
    PEMU_APC_RETURN ApcReturn;
    LIST_ENTRY Link;
    HANDLE ParkEvent;
    HANDLE ReleaseEvent;
    PEMU_GUEST_CALL GuestCall;
    EMU_STACK GuestStack;
    CHPE_V2_CPU_AREA_INFO CpuArea;
    PEMU_BOUNDARY Boundary;
    EMU_STACK EmulatorStack;
    EMU_TEB_STACK GuestTebStack;
} EMU_THREAD, *PEMU_THREAD;

typedef struct _EMU_THREAD_START
{
    AMD64_CONTEXT Context;
} EMU_THREAD_START, *PEMU_THREAD_START;

typedef struct _EMU_PROCESS
{
    BOOLEAN Attached;
    PPEB GuestPeb;
    PVOID GuestNtdll;
    ULONG64 LdrInitializeThunk;
    ULONG64 KiUserExceptionDispatcher;
    ULONG64 KiUserCallbackDispatcher;
    ULONG64 KiUserApcDispatcher;
    ULONG64 RtlUserThreadStart;
    ULONG_PTR NativeRtlUserThreadStart;
    UNICODE_STRING SystemDirectory;
    UNICODE_STRING SystemRootDirectory;
    UNICODE_STRING GuestDirectory;
    WCHAR SystemDirectoryBuffer[MAX_PATH];
    WCHAR GuestDirectoryBuffer[MAX_PATH];
    RTL_CRITICAL_SECTION ThreadLock;
    LIST_ENTRY Threads;
} EMU_PROCESS, *PEMU_PROCESS;

extern EMU_PROCESS EmuProcess;

NTSYSAPI
NTSTATUS
NTAPI
RtlCreateUserStack(
    _In_opt_ SIZE_T CommittedStackSize,
    _In_opt_ SIZE_T MaximumStackSize,
    _In_opt_ ULONG ZeroBits,
    _In_ SIZE_T CommitAlignment,
    _In_ SIZE_T ReserveAlignment,
    _Out_ PINITIAL_TEB InitialTeb);

NTSYSAPI
VOID
NTAPI
RtlFreeUserStack(
    _In_ PVOID AllocationBase);

NTSYSAPI
PVOID
NTAPI
RtlFindExportedRoutineByName(
    _In_ PVOID ImageBase,
    _In_ PCSTR Name);

PEMU_THREAD
EmuAttachedThread(VOID);

PEMU_THREAD
EmuEnterAttachedThread(VOID);

FORCEINLINE
PEMU_THREAD
EmuCurrentThread(VOID)
{
    return EmuProcess.Attached ? EmuAttachedThread() : EmulationCurrentThread()->HostData;
}

VOID
NTAPI
EmuThreadStart(
    _In_ PEMULATION_THREAD Host);

VOID
NTAPI
EmuGuestThreadEntry(
    _In_ PVOID Start);

DECLSPEC_NORETURN
VOID
EmuFatal(
    _In_ NTSTATUS Status,
    _In_ PCSTR Message);

#define EMU_BRIDGE_SERVICE (EMU_HOST_SERVICE_BASE + 0x20000)

ULONG64
EmuCreateStub(
    _In_reads_(Length) const UCHAR *Code,
    _In_ ULONG Length);

ULONG64
EmuBridgeEntry(
    _In_ ULONG64 Rip);

BOOLEAN
EmuBridgeService(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG Service,
    _Inout_ PEMU_REGISTERS Registers);

VOID
NTAPI
UpdateProcessorInformation(
    _Inout_ PVOID ProcessorInformation);

VOID
EmuBuildNativeStartContext(
    _In_ const CONTEXT *Native,
    _Out_ AMD64_CONTEXT *Start);

NTSTATUS
EmuAdoptGuestImage(
    _In_ PVOID ImageBase);

NTSTATUS
EmuMapGuestImage(
    _In_ PCUNICODE_STRING Name,
    _Out_ PVOID *ImageBase);

NTSTATUS
EmuStartGuest(
    _In_ PEMU_THREAD Thread,
    _In_ const AMD64_CONTEXT *Start);

NTSTATUS
EmuCreateThread(
    _In_ ULONG Service,
    _Inout_ PULONG_PTR Arguments);

DECLSPEC_NORETURN
VOID
EmuExitThread(
    _In_ PEMU_THREAD Thread,
    _In_ NTSTATUS ExitStatus);

NTSTATUS
EmuRegisterThread(
    _In_ PEMU_THREAD Thread);

VOID
EmuUnregisterThread(
    _In_ PEMU_THREAD Thread);

NTSTATUS
EmuSuspendThread(
    _In_ HANDLE ThreadHandle,
    _Out_opt_ PULONG PreviousSuspendCount);

NTSTATUS
EmuGetContextThread(
    _In_ HANDLE ThreadHandle,
    _Inout_ AMD64_CONTEXT *Context);

NTSTATUS
EmuSetContextThread(
    _In_ PEMU_THREAD Thread,
    _In_ HANDLE ThreadHandle,
    _In_ const AMD64_CONTEXT *Context,
    _Out_ PBOOLEAN Resumed);

VOID
EmuParkThread(
    _In_ PEMU_THREAD Thread);

NTSTATUS
EmuDispatchException(
    _In_ PEMU_THREAD Thread,
    _In_ const EXCEPTION_RECORD *Record);

NTSTATUS
EmuContinue(
    _In_ PEMU_THREAD Thread,
    _In_ const AMD64_CONTEXT *Context);

NTSTATUS
EmuRaiseException(
    _In_ PEMU_THREAD Thread,
    _In_ const EXCEPTION_RECORD *Record,
    _In_ const AMD64_CONTEXT *Context,
    _In_ BOOLEAN FirstChance);

LONG
NTAPI
EmuVectoredHandler(
    _In_ PEXCEPTION_POINTERS ExceptionInfo);

VOID
EmuInitializeCallbacks(VOID);

BOOLEAN
EmuIsApcReturn(
    _In_ PEMU_THREAD Thread,
    _In_ const AMD64_CONTEXT *Context);

NTSTATUS
EmuApcReturn(
    _In_ PEMU_THREAD Thread,
    _In_ const AMD64_CONTEXT *Context);

NTSTATUS
EmuQueueUserApc(
    _In_ ULONG Service,
    _Inout_ PULONG_PTR Arguments,
    _In_ ULONG RoutineIndex);

BOOLEAN
EmuWrapCompletionApc(
    _Inout_ PULONG_PTR Arguments,
    _In_ ULONG RoutineIndex,
    _In_ BOOLEAN Persistent,
    _Out_ PVOID *Record);

VOID
EmuCompleteApcWrap(
    _In_opt_ PVOID Record,
    _In_ NTSTATUS Status);

NTSTATUS
EmuCallbackReturn(
    _In_ PEMU_THREAD Thread,
    _In_opt_ PVOID Result,
    _In_ ULONG ResultLength,
    _In_ NTSTATUS CallbackStatus);

BOOLEAN
EmuSoftwareInterrupt(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG Vector,
    _Inout_ PEXCEPTION_RECORD Record);

BOOLEAN
EmuRedirectObject(
    _In_ POBJECT_ATTRIBUTES Source,
    _Out_ POBJECT_ATTRIBUTES Redirected,
    _Out_ PUNICODE_STRING Name,
    _Out_writes_bytes_(BufferSize) PWCHAR Buffer,
    _In_ ULONG BufferSize);

BOOLEAN
EmuIsKnownDllDirectory(
    _In_ POBJECT_ATTRIBUTES ObjectAttributes);

ULONG_PTR
NTAPI
EmuInvokeSystemService(
    _In_ ULONG Service,
    _In_reads_(EMU_MAX_SERVICE_ARGUMENTS) PULONG_PTR Arguments);

ULONG64
EmuInvokeNative(
    _In_ PVOID Function,
    _In_reads_(8) const ULONG64 *Registers,
    _In_reads_opt_(StackCount) const ULONG64 *Stack,
    _In_ ULONG StackCount);

NTSTATUS
EmuInitializeHostCalls(VOID);

VOID
EmuHostService(
    _In_ PEMU_THREAD Thread,
    _In_ ULONG Service,
    _Inout_ PEMU_REGISTERS Registers);

ULONG64
EmuNativeEntry(
    _In_ ULONG64 Rip);

#define EMU_DISPATCH_SERVICE (EMU_HOST_SERVICE_BASE + 2)

extern ULONG64 EmuGuestReturnStub;

ULONG64
NTAPI
EmuCallGuest(
    _In_ ULONG64 Function,
    _In_ PCSTR Signature,
    _In_opt_ const ULONG64 *Arguments);

DECLSPEC_NORETURN
VOID
EmuRunGuest(
    _In_ PEMU_CPU Cpu,
    _Out_ PULONG64 RunFrame,
    _In_ ULONG64 EmulatorSp);

VOID
EmuEnterBoundary(
    _In_ PEMU_BOUNDARY Boundary);

VOID
EmuX64SystemService(
    _In_ ULONG Service,
    _Inout_ PEMU_REGISTERS Registers);

VOID
EmuBoundaryRoutine(
    _In_ PEMU_BOUNDARY Boundary);

VOID
EmuBoundaryContinue(
    _In_ PEMU_BOUNDARY Boundary);

EXCEPTION_DISPOSITION
NTAPI
EmuBoundaryHandler(
    _Inout_ PEXCEPTION_RECORD Record,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT NativeContext,
    _Inout_ PVOID NativeDispatcherContext);

NTSTATUS
EmuX64InitializeDispatch(VOID);

NTSTATUS
EmuInitializeServiceEntries(VOID);

VOID
EmuX64DispatchService(
    _In_ PEMU_THREAD Thread,
    _Inout_ PEMU_REGISTERS Registers);

LONG
EmuX64CallVectoredHandlers(
    _In_ PEMU_THREAD Thread,
    _In_ PEXCEPTION_RECORD Record,
    _In_ BOOLEAN ContinueHandlers);

PVOID
EmuX64AddVectoredHandler(
    _In_ BOOLEAN ContinueHandler,
    _In_ ULONG First,
    _In_ ULONG64 Handler);

ULONG
EmuX64RemoveVectoredHandler(
    _In_ BOOLEAN ContinueHandler,
    _In_ PVOID Handle);

PEMU_AMD64_RUNTIME_FUNCTION
EmuX64LookupFunctionEntry(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase);

PEMU_AMD64_RUNTIME_FUNCTION
EmuX64LookupFunctionTable(
    _In_ ULONG64 ControlPc,
    _Out_ PULONG64 ImageBase,
    _Out_ PULONG Length);

ULONG64
EmuX64VirtualUnwind(
    _In_ ULONG HandlerType,
    _In_ ULONG64 ImageBase,
    _In_ ULONG64 ControlPc,
    _In_opt_ PEMU_AMD64_RUNTIME_FUNCTION FunctionEntry,
    _Inout_ AMD64_CONTEXT *Context,
    _Out_opt_ PVOID *HandlerData,
    _Out_opt_ PULONG64 EstablisherFrame,
    _Inout_opt_ PEMU_AMD64_CONTEXT_POINTERS ContextPointers);

DECLSPEC_NORETURN
VOID
EmuX64UnwindEx(
    _In_ PEMU_THREAD Thread,
    _In_opt_ ULONG64 TargetFrame,
    _In_opt_ ULONG64 TargetIp,
    _In_opt_ PEXCEPTION_RECORD Record,
    _In_ ULONG64 ReturnValue);

VOID
EmuX64RestoreContext(
    _In_ PEMU_THREAD Thread,
    _Inout_ AMD64_CONTEXT *Context,
    _In_opt_ PEXCEPTION_RECORD Record);

EXCEPTION_DISPOSITION
EmuX64CSpecificHandler(
    _Inout_ PEXCEPTION_RECORD Record,
    _In_ ULONG64 EstablisherFrame,
    _Inout_ AMD64_CONTEXT *Context,
    _Inout_ PEMU_AMD64_DISPATCHER_CONTEXT DispatcherContext);
