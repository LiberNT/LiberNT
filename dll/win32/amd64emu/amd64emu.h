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
