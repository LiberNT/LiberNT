/*
 * PROJECT:     LiberNT Watchdog
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Session change callouts between the graphics kernel and win32k
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <ndk/psfuncs.h>

#define SMGR_MAX_CALLOUTS 8
#define SMGR_MAX_SESSIONS 16

/* FIXME: the meaning of the parameters is INFO_NEEDED; a session start passes (0, NULL, 0, 1) */
typedef NTSTATUS
(NTAPI *PSMGR_SESSION_CHANGE_CALLOUT)(
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Parameter3,
    _In_opt_ PVOID Parameter4);

typedef struct _SMGR_CALLOUT
{
    PSMGR_SESSION_CHANGE_CALLOUT Callout;
    PVOID Parameter2;
    ULONG_PTR Parameter3;
    PVOID Parameter4;
    ULONG Parameter5;
} SMGR_CALLOUT, *PSMGR_CALLOUT;

typedef struct _SMGR_SESSION
{
    ULONG SessionId;
    PVOID Win32kCallout;
} SMGR_SESSION, *PSMGR_SESSION;

static ERESOURCE SmgrLock;
static volatile LONG SmgrLockState;
static SMGR_CALLOUT SmgrCallouts[SMGR_MAX_CALLOUTS];
static SMGR_SESSION SmgrSessions[SMGR_MAX_SESSIONS];

static
VOID
SmgrEnsureLock(VOID)
{
    LONG State;

    State = InterlockedCompareExchange(&SmgrLockState, 1, 0);
    if (State == 0)
    {
        ExInitializeResourceLite(&SmgrLock);
        InterlockedExchange(&SmgrLockState, 2);
        return;
    }

    while (ReadAcquire(&SmgrLockState) != 2)
        YieldProcessor();
}

static
VOID
SmgrAcquire(
    _In_ BOOLEAN Exclusive)
{
    PAGED_CODE();

    SmgrEnsureLock();
    KeEnterCriticalRegion();
    if (Exclusive)
        ExAcquireResourceExclusiveLite(&SmgrLock, TRUE);
    else
        ExAcquireResourceSharedLite(&SmgrLock, TRUE);
}

static
VOID
SmgrRelease(VOID)
{
    ASSERT(ExIsResourceAcquiredLite(&SmgrLock));
    ExReleaseResourceLite(&SmgrLock);
    KeLeaveCriticalRegion();
}

/* FIXME: the meaning of the second to fifth parameters is INFO_NEEDED; they are kept as given */
NTSTATUS
NTAPI
SMgrRegisterSessionChangeCallout(
    _In_ PVOID Callout,
    _In_opt_ PVOID Parameter2,
    _In_ ULONG_PTR Parameter3,
    _In_opt_ PVOID Parameter4,
    _In_ ULONG Parameter5)
{
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;
    ULONG Index;

    PAGED_CODE();

    if (Callout == NULL)
        return STATUS_INVALID_PARAMETER_1;

    SmgrAcquire(TRUE);
    for (Index = 0; Index < SMGR_MAX_CALLOUTS; Index++)
    {
        if (SmgrCallouts[Index].Callout == NULL)
        {
            SmgrCallouts[Index].Callout = (PSMGR_SESSION_CHANGE_CALLOUT)Callout;
            SmgrCallouts[Index].Parameter2 = Parameter2;
            SmgrCallouts[Index].Parameter3 = Parameter3;
            SmgrCallouts[Index].Parameter4 = Parameter4;
            SmgrCallouts[Index].Parameter5 = Parameter5;
            Status = STATUS_SUCCESS;
            break;
        }
    }
    SmgrRelease();

    return Status;
}

/* FIXME: the parameters are INFO_NEEDED; the registration whose callout is the first parameter is removed */
NTSTATUS
NTAPI
SMgrUnregisterSessionChangeCallout(
    _In_ PVOID Callout)
{
    NTSTATUS Status = STATUS_NOT_FOUND;
    ULONG Index;

    PAGED_CODE();

    SmgrAcquire(TRUE);
    for (Index = 0; Index < SMGR_MAX_CALLOUTS; Index++)
    {
        if (Callout != NULL && SmgrCallouts[Index].Callout == (PSMGR_SESSION_CHANGE_CALLOUT)Callout)
        {
            RtlZeroMemory(&SmgrCallouts[Index], sizeof(SmgrCallouts[Index]));
            Status = STATUS_SUCCESS;
            break;
        }
    }
    SmgrRelease();

    return Status;
}

static
NTSTATUS
SmgrRememberWin32kCallout(
    _In_ ULONG SessionId,
    _In_opt_ PVOID Win32kCallout)
{
    PSMGR_SESSION Free = NULL;
    ULONG Index;

    ASSERT(ExIsResourceAcquiredExclusiveLite(&SmgrLock));

    for (Index = 0; Index < SMGR_MAX_SESSIONS; Index++)
    {
        if (SmgrSessions[Index].Win32kCallout != NULL && SmgrSessions[Index].SessionId == SessionId)
        {
            SmgrSessions[Index].Win32kCallout = Win32kCallout;
            return STATUS_SUCCESS;
        }
        if (Free == NULL && SmgrSessions[Index].Win32kCallout == NULL)
            Free = &SmgrSessions[Index];
    }

    if (Win32kCallout == NULL)
        return STATUS_SUCCESS;
    if (Free == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Free->SessionId = SessionId;
    Free->Win32kCallout = Win32kCallout;
    return STATUS_SUCCESS;
}

/* FIXME: the meaning of the first, third, fourth and fifth parameters is INFO_NEEDED */
NTSTATUS
NTAPI
SMgrNotifySessionChange(
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Win32kCallout,
    _In_opt_ PVOID Parameter3,
    _In_opt_ PVOID Parameter4,
    _In_ ULONG Parameter5)
{
    ULONG SessionId;
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(Parameter4);
    UNREFERENCED_PARAMETER(Parameter5);

    SessionId = PsGetCurrentProcessSessionId();

    SmgrAcquire(TRUE);
    Status = SmgrRememberWin32kCallout(SessionId, Win32kCallout);
    SmgrRelease();
    if (!NT_SUCCESS(Status))
        return Status;

    SmgrAcquire(FALSE);
    for (Index = 0; Index < SMGR_MAX_CALLOUTS; Index++)
    {
        if (SmgrCallouts[Index].Callout == NULL)
            continue;

        /* FIXME: a session start passes the fourth parameter as 1; other session changes are INFO_NEEDED */
        SmgrCallouts[Index].Callout(Parameter1, NULL, Parameter3, (PVOID)(ULONG_PTR)1);
    }
    SmgrRelease();

    return STATUS_SUCCESS;
}

/* FIXME: the parameters and the structure handed to the win32k callout are INFO_NEEDED */
NTSTATUS
NTAPI
SMgrGdiCallout(
    _In_ PVOID Parameters,
    _In_ ULONG Parameter2,
    _In_ BOOLEAN Parameter3,
    _In_opt_ PVOID Parameter4,
    _In_opt_ PVOID Parameter5)
{
    UNREFERENCED_PARAMETER(Parameters);
    UNREFERENCED_PARAMETER(Parameter2);
    UNREFERENCED_PARAMETER(Parameter3);
    UNREFERENCED_PARAMETER(Parameter4);
    UNREFERENCED_PARAMETER(Parameter5);

    PAGED_CODE();
    return STATUS_NOT_SUPPORTED;
}
