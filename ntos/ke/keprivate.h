/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What the kernel core's own files say to each other
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/*
 * These are the routines the files under ntos/ke share among themselves and
 * that no published header declares. Nothing outside this directory should
 * call them: a caller elsewhere means the routine belongs in internal/ke.h
 * instead, where its contract can be stated once for everyone.
 */

/* schedule.c: puts a ready thread on a named processor's queues, which may be
   a processor other than the caller's. Entered with that processor's PRCB lock
   held and released before return. */
VOID
FASTCALL
KiQueueReadyThread(
    _Inout_ PKTHREAD Thread,
    _Inout_ PKPRCB Prcb);

/* dpcobj.c: the shared part of describing a deferred call, with the object
   type the caller wants it to have. */
VOID
NTAPI
KiInitializeDpc(
    _Out_ PKDPC Dpc,
    _In_ PKDEFERRED_ROUTINE DeferredRoutine,
    _In_opt_ PVOID DeferredContext,
    _In_ UCHAR Type);

/* ktimer.c: walks the whole timer table looking for a timer that came due and
   was never expired. Reports what it finds and changes nothing. */
VOID
NTAPI
KiCheckTimerTable(
    _In_ ULARGE_INTEGER CurrentTime);

/* profile.c: records one sample against every profile on a list that is
   watching the address. Entered with the profile lock held. */
VOID
NTAPI
KiParseProfileList(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KPROFILE_SOURCE Source,
    _In_ PLIST_ENTRY ListHead);

/* crash.c: the stop screen, and the pieces it is built from. */
VOID
NTAPI
KiDisplayBlueScreen(
    _In_ ULONG BugCheckCode,
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PCONTEXT Context);

VOID
NTAPI
KiBugCheckDebugBreak(
    _In_ ULONG StatusCode);

VOID
NTAPI
KiDoBugCheckCallbacks(VOID);

VOID
NTAPI
KiDumpParameterImages(
    _In_z_ PCHAR Message,
    _In_ PULONG_PTR Parameters,
    _In_ ULONG ParameterCount,
    _In_ PKE_BUGCHECK_UNICODE_TO_ANSI Converter);

VOID
NTAPI
KeGetBugMessageText(
    _In_ ULONG BugCheckCode,
    _Out_opt_ PANSI_STRING OutputString);

VOID
NTAPI
KeRosDumpStackFrameArray(
    _In_ PULONG_PTR Frames,
    _In_ ULONG FrameCount);

ULONG
NTAPI
KeRosCaptureUserStackBackTrace(
    _In_ ULONG FramesToSkip,
    _In_ ULONG FramesToCapture,
    _Out_ PVOID *BackTrace,
    _Out_opt_ PULONG BackTraceHash);

/* EOF */
