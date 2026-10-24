/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Interface between the kernel and a pico process provider
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _REACTOS_PICO_H_
#define _REACTOS_PICO_H_

typedef
VOID
NTAPI
PICO_SYSTEM_CALL_DISPATCH(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame);
typedef PICO_SYSTEM_CALL_DISPATCH *PPICO_SYSTEM_CALL_DISPATCH;

typedef
BOOLEAN
NTAPI
PICO_DISPATCH_EXCEPTION(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Inout_ PKTRAP_FRAME TrapFrame);
typedef PICO_DISPATCH_EXCEPTION *PPICO_DISPATCH_EXCEPTION;

typedef
VOID
NTAPI
PICO_THREAD_START(
    _Inout_ PKTRAP_FRAME TrapFrame,
    _Inout_opt_ PKEXCEPTION_FRAME ExceptionFrame);
typedef PICO_THREAD_START *PPICO_THREAD_START;

typedef
VOID
NTAPI
PICO_THREAD_EXIT(
    _In_ PETHREAD Thread);
typedef PICO_THREAD_EXIT *PPICO_THREAD_EXIT;

typedef
VOID
NTAPI
PICO_PROCESS_EXIT(
    _In_ PEPROCESS Process);
typedef PICO_PROCESS_EXIT *PPICO_PROCESS_EXIT;

typedef struct _PICO_PROVIDER_ROUTINES
{
    SIZE_T Size;
    PPICO_SYSTEM_CALL_DISPATCH SystemCallDispatch;
    PPICO_DISPATCH_EXCEPTION DispatchException;
    PPICO_THREAD_START ThreadStart;
    PPICO_THREAD_EXIT ThreadExit;
    PPICO_PROCESS_EXIT ProcessExit;
} PICO_PROVIDER_ROUTINES, *PPICO_PROVIDER_ROUTINES;

NTKERNELAPI
NTSTATUS
NTAPI
PsPicoRegisterProvider(
    _In_ const PICO_PROVIDER_ROUTINES *Routines);

NTKERNELAPI
NTSTATUS
NTAPI
PsPicoCreateProcess(
    _In_ BOOLEAN CloneCurrent,
    _In_opt_ PVOID PicoContext,
    _Out_ PHANDLE ProcessHandle);

NTKERNELAPI
NTSTATUS
NTAPI
PsPicoCreateThread(
    _In_ HANDLE ProcessHandle,
    _In_ PCONTEXT Context,
    _In_opt_ PVOID PicoContext,
    _Out_ PHANDLE ThreadHandle,
    _Out_opt_ PCLIENT_ID ClientId);

DECLSPEC_NORETURN
NTKERNELAPI
VOID
NTAPI
PsPicoTerminateCurrentThread(
    _In_ NTSTATUS ExitStatus);

NTKERNELAPI
NTSTATUS
NTAPI
PsPicoTerminateThread(
    _In_ PETHREAD Thread,
    _In_ NTSTATUS ExitStatus);

NTKERNELAPI
PVOID
NTAPI
PsPicoGetProcessContext(
    _In_ PEPROCESS Process);

NTKERNELAPI
PVOID
NTAPI
PsPicoGetThreadContext(
    _In_ PETHREAD Thread);

#endif
