/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The routines a driver asks to have called while a live kernel
 *              dump is being taken
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/*
 * How many routines may be registered at once. A dump has to be taken while
 * the machine is stopped, so the list is kept short on purpose.
 */
#define PSP_MAX_LKMD_CALLBACKS 8

/* The only flag a caller may pass: whether its routine wants the secondary
 * data pass as well as the first */
#define PSP_LKMD_VALID_FLAGS 0x1

/* GLOBALS ********************************************************************/

typedef struct _PSP_LKMD_CALLBACK
{
    PVOID Callback;
    PVOID Context;
    ULONG Flags;
    BOOLEAN Used;
} PSP_LKMD_CALLBACK, *PPSP_LKMD_CALLBACK;

static PSP_LKMD_CALLBACK PspLkmdCallbacks[PSP_MAX_LKMD_CALLBACKS];
static KSPIN_LOCK PspLkmdLock;
static LONG PspLkmdLockReady;

/* PRIVATE FUNCTIONS **********************************************************/

static
KIRQL
PspLkmdAcquire(VOID)
{
    KIRQL OldIrql;

    /*
     * The lock is built on first use: a driver may register before anything
     * in the process manager has had a chance to initialize this.
     */
    if (InterlockedCompareExchange(&PspLkmdLockReady, 1, 0) == 0)
        KeInitializeSpinLock(&PspLkmdLock);

    KeAcquireSpinLock(&PspLkmdLock, &OldIrql);
    return OldIrql;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Registers a routine to be called while a live dump is taken. A routine
 * that is already registered is refused rather than being called twice, and
 * a caller is told when there is no room left rather than being ignored.
 */
NTSTATUS
NTAPI
DbgkLkmdRegisterCallback(
    _In_ PVOID Callback,
    _In_opt_ PVOID Context,
    _In_ ULONG Flags)
{
    KIRQL OldIrql;
    ULONG Index;
    ULONG Free = PSP_MAX_LKMD_CALLBACKS;

    if ((Flags & ~PSP_LKMD_VALID_FLAGS) != 0) return STATUS_INVALID_PARAMETER;

    OldIrql = PspLkmdAcquire();

    for (Index = 0; Index < PSP_MAX_LKMD_CALLBACKS; Index++)
    {
        if (!PspLkmdCallbacks[Index].Used)
        {
            if (Free == PSP_MAX_LKMD_CALLBACKS) Free = Index;
            continue;
        }

        if (PspLkmdCallbacks[Index].Callback == Callback)
        {
            KeReleaseSpinLock(&PspLkmdLock, OldIrql);
            return STATUS_ALREADY_REGISTERED;
        }
    }

    if (Free == PSP_MAX_LKMD_CALLBACKS)
    {
        KeReleaseSpinLock(&PspLkmdLock, OldIrql);
        return STATUS_IMPLEMENTATION_LIMIT;
    }

    PspLkmdCallbacks[Free].Callback = Callback;
    PspLkmdCallbacks[Free].Context = Context;
    PspLkmdCallbacks[Free].Flags = Flags;
    PspLkmdCallbacks[Free].Used = TRUE;

    KeReleaseSpinLock(&PspLkmdLock, OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DbgkLkmdUnregisterCallback(
    _In_ PVOID Callback)
{
    KIRQL OldIrql;
    ULONG Index;

    OldIrql = PspLkmdAcquire();

    for (Index = 0; Index < PSP_MAX_LKMD_CALLBACKS; Index++)
    {
        if (!PspLkmdCallbacks[Index].Used) continue;
        if (PspLkmdCallbacks[Index].Callback != Callback) continue;

        RtlZeroMemory(&PspLkmdCallbacks[Index],
                      sizeof(PspLkmdCallbacks[Index]));

        KeReleaseSpinLock(&PspLkmdLock, OldIrql);
        return STATUS_SUCCESS;
    }

    KeReleaseSpinLock(&PspLkmdLock, OldIrql);
    return STATUS_NOT_FOUND;
}
