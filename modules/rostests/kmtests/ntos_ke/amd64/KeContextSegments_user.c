/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Segment selectors of a user-mode context resumed through NtContinue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndk/kefuncs.h>

#define SEGMENT_EXIT_CODE 0x5A5A0002
#define SEGMENT_INVALID_SELECTOR 0xFFF8

typedef struct _SEGMENT_STATE
{
    WORD Ds;
    WORD Es;
    WORD Fs;
    WORD Gs;
} SEGMENT_STATE;

static SEGMENT_STATE Original;
static SEGMENT_STATE Resumed;

static
VOID
ReadSegments(
    _Out_ SEGMENT_STATE *State)
{
    __asm__ __volatile__("movw %%ds, %0" : "=r"(State->Ds));
    __asm__ __volatile__("movw %%es, %0" : "=r"(State->Es));
    __asm__ __volatile__("movw %%fs, %0" : "=r"(State->Fs));
    __asm__ __volatile__("movw %%gs, %0" : "=r"(State->Gs));
}

static
DECLSPEC_NORETURN
VOID
NTAPI
SegmentResume(VOID)
{
    ReadSegments(&Resumed);
    ExitThread(SEGMENT_EXIT_CODE);
}

static
DWORD
WINAPI
SegmentContinueThread(
    _In_ PVOID Parameter)
{
    CONTEXT Context;

    UNREFERENCED_PARAMETER(Parameter);

    ReadSegments(&Original);
    RtlCaptureContext(&Context);
    Context.ContextFlags = CONTEXT_FULL | CONTEXT_SEGMENTS;
    Context.SegDs = SEGMENT_INVALID_SELECTOR;
    Context.SegEs = SEGMENT_INVALID_SELECTOR;
    Context.SegFs = SEGMENT_INVALID_SELECTOR;
    Context.SegGs = SEGMENT_INVALID_SELECTOR;
    Context.Rsp = (Context.Rsp & ~(DWORD64)15) - 0x108;
    Context.Rip = (DWORD64)SegmentResume;
    NtContinue(&Context, FALSE);
    return 0;
}

START_TEST(KeContextSegments)
{
    HANDLE Thread;
    DWORD Wait, ExitCode = 0;

    RtlZeroMemory(&Original, sizeof(Original));
    RtlZeroMemory(&Resumed, sizeof(Resumed));
    Thread = CreateThread(NULL, 0x10000, SegmentContinueThread, NULL, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Thread)
        return;

    Wait = WaitForSingleObject(Thread, 10000);
    ok_eq_ulong(Wait, WAIT_OBJECT_0);
    if (Wait != WAIT_OBJECT_0)
    {
        TerminateThread(Thread, 0);
        CloseHandle(Thread);
        return;
    }

    ok(GetExitCodeThread(Thread, &ExitCode), "GetExitCodeThread failed with %lu\n", GetLastError());
    ok_eq_hex(ExitCode, SEGMENT_EXIT_CODE);
    ok_eq_uint(Resumed.Ds, Original.Ds);
    ok_eq_uint(Resumed.Es, Original.Es);
    ok_eq_uint(Resumed.Fs, Original.Fs);
    ok_eq_uint(Resumed.Gs, Original.Gs);

    CloseHandle(Thread);
}
