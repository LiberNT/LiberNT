/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for RtlTryAcquirePebLock
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define PEB_LOCK_TIMEOUT 5000

typedef LOGICAL (NTAPI *FN_RtlTryAcquirePebLock)(VOID);

static FN_RtlTryAcquirePebLock pRtlTryAcquirePebLock;

typedef struct _PEB_LOCK_HOLDER
{
    HANDLE Held;
    HANDLE Release;
} PEB_LOCK_HOLDER, *PPEB_LOCK_HOLDER;

static
DWORD
WINAPI
PebLockHolder(
    _In_ PVOID Parameter)
{
    PPEB_LOCK_HOLDER Holder = Parameter;

    RtlAcquirePebLock();
    SetEvent(Holder->Held);
    WaitForSingleObject(Holder->Release, PEB_LOCK_TIMEOUT);
    RtlReleasePebLock();
    return 0;
}

START_TEST(RtlTryAcquirePebLock)
{
    PRTL_CRITICAL_SECTION PebLock = NtCurrentPeb()->FastPebLock;
    PEB_LOCK_HOLDER Holder;
    HANDLE Thread;
    LOGICAL Result;
    DWORD Wait;

    pRtlTryAcquirePebLock = (FN_RtlTryAcquirePebLock)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                                    "RtlTryAcquirePebLock");
    ok(pRtlTryAcquirePebLock != NULL, "RtlTryAcquirePebLock is missing\n");
    if (pRtlTryAcquirePebLock == NULL)
        return;

    Result = pRtlTryAcquirePebLock();
    ok_eq_ulong(Result, TRUE);
    if (Result)
    {
        ok_eq_ulong(RtlIsCriticalSectionLockedByThread(PebLock), TRUE);
        Result = pRtlTryAcquirePebLock();
        ok_eq_ulong(Result, TRUE);
        if (Result)
        {
            ok_eq_long(PebLock->RecursionCount, 2);
            RtlReleasePebLock();
        }
        RtlReleasePebLock();
    }
    ok_eq_ulong(RtlIsCriticalSectionLockedByThread(PebLock), FALSE);

    Holder.Held = CreateEventW(NULL, TRUE, FALSE, NULL);
    Holder.Release = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Holder.Held != NULL && Holder.Release != NULL, "CreateEventW failed with %lu\n", GetLastError());
    if (Holder.Held == NULL || Holder.Release == NULL)
        return;

    Thread = CreateThread(NULL, 0, PebLockHolder, &Holder, 0, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (Thread != NULL)
    {
        Wait = WaitForSingleObject(Holder.Held, PEB_LOCK_TIMEOUT);
        Result = (Wait == WAIT_OBJECT_0) ? pRtlTryAcquirePebLock() : 2;
        if (Result == TRUE)
            RtlReleasePebLock();
        SetEvent(Holder.Release);
        ok_eq_ulong(WaitForSingleObject(Thread, PEB_LOCK_TIMEOUT), WAIT_OBJECT_0);
        CloseHandle(Thread);
        ok_eq_ulong(Wait, WAIT_OBJECT_0);
        ok_eq_ulong(Result, FALSE);
    }

    Result = pRtlTryAcquirePebLock();
    ok_eq_ulong(Result, TRUE);
    if (Result)
        RtlReleasePebLock();

    CloseHandle(Holder.Held);
    CloseHandle(Holder.Release);
}
