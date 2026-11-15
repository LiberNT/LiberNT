/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User-mode debugging internals
 * COPYRIGHT:   Copyright Alex Ionescu <alex.ionescu@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

extern FAST_MUTEX DbgkpProcessDebugPortMutex;

extern ULONG DbgkpTraceLevel;

extern POBJECT_TYPE DbgkDebugObjectType;

VOID
NTAPI
DbgkCloseThreadDebugObject(
    _In_ PETHREAD Thread);

CODE_SEG("INIT")
VOID
NTAPI
DbgkInitialize(VOID);

NTSTATUS
NTAPI
DbgkpQueueMessage(
    _In_ PEPROCESS Process,
    _In_ PETHREAD Thread,
    _Inout_ PDBGKM_MSG ApiMsg,
    _In_ ULONG Flags,
    _In_opt_ PDEBUG_OBJECT TargetObject);

VOID
NTAPI
DbgkpWakeTarget(
    _In_ PDEBUG_EVENT DebugEvent);

NTSTATUS
NTAPI
DbgkClearProcessDebugObject(
    _In_ PEPROCESS Process,
    _In_ PDEBUG_OBJECT SourceDebugObject);

VOID
NTAPI
DbgkCopyProcessDebugPort(
    _In_ PEPROCESS Process,
    _In_ PEPROCESS Parent);

VOID
NTAPI
DbgkCreateThread(
    _In_ PETHREAD Thread,
    _In_ PVOID StartAddress);

VOID
NTAPI
DbgkExitProcess(
    _In_ NTSTATUS ExitStatus);

VOID
NTAPI
DbgkExitThread(
    _In_ NTSTATUS ExitStatus);

BOOLEAN
NTAPI
DbgkForwardException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ BOOLEAN DebugPort,
    _In_ BOOLEAN SecondChance);

VOID
NTAPI
DbgkMapViewOfSection(
    _In_ PVOID Section,
    _In_ PVOID BaseAddress,
    _In_ ULONG SectionOffset,
    _In_ ULONG_PTR ViewSize);

NTSTATUS
NTAPI
DbgkOpenProcessDebugPort(
    _In_ PEPROCESS Process,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ HANDLE *DebugHandle);

VOID
NTAPI
DbgkUnMapViewOfSection(
    _In_ PVOID BaseAddress);

VOID
NTAPI
DbgkpResumeProcess(VOID);

HANDLE
NTAPI
DbgkpSectionToFileHandle(
    _In_ PVOID Section);

NTSTATUS
NTAPI
DbgkpSendApiMessage(
    _Inout_ PDBGKM_MSG ApiMsg,
    _In_ BOOLEAN SuspendProcess);

BOOLEAN
NTAPI
DbgkpSuspendProcess(VOID);
