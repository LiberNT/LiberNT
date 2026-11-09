/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Which components are allowed to be heard, and at what level
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdpriv.h"
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * A level below 32 names one bit of the component's mask; anything else is
 * already a set of bits. This is how a caller can ask about several levels at
 * once, and why level 31 and the mask 0x80000000 mean the same thing.
 */
static
ULONG
KdpLevelToMask(
    _In_ ULONG Level)
{
    if (Level < 32)
        return (1 << Level);

    return Level;
}

static
PULONG
KdpComponentMask(
    _In_ ULONG ComponentId)
{
    if (ComponentId >= KdComponentTableSize)
        return NULL;

    return KdComponentTable[ComponentId];
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Whether something printed by this component at this level is to be shown.
 * The system-wide mask answers for every component, so a component nobody has
 * configured is still heard when the system-wide mask allows the level.
 */
BOOLEAN
NTAPI
KdpFilterAllows(
    _In_ ULONG ComponentId,
    _In_ ULONG Level)
{
    ULONG Mask = KdpLevelToMask(Level);
    PULONG ComponentMask = KdpComponentMask(ComponentId);

    if ((Kd_WIN2000_Mask & Mask) != 0)
        return TRUE;

    if (ComponentMask == NULL)
    {
        /*
         * A component this kernel has never heard of is judged by the
         * default component, as that is the one the caller would have used
         * had it known it had no entry of its own.
         */
        ComponentMask = &Kd_DEFAULT_Mask;
    }

    return ((*ComponentMask & Mask) != 0);
}

/*
 * Answers whether a component's output at a level is currently shown. The
 * answer is the value TRUE or FALSE returned as a status, which is why a
 * caller must compare it and not test it for success.
 */
NTSTATUS
NTAPI
NtQueryDebugFilterState(
    _In_ ULONG ComponentId,
    _In_ ULONG Level)
{
    if (ComponentId >= KdComponentTableSize)
        return STATUS_INVALID_PARAMETER_1;

    return KdpFilterAllows(ComponentId, Level) ? (NTSTATUS)TRUE : (NTSTATUS)FALSE;
}

/*
 * Turns one component's output at one level on or off. Only a caller that may
 * debug the machine gets to change what the machine says.
 */
NTSTATUS
NTAPI
NtSetDebugFilterState(
    _In_ ULONG ComponentId,
    _In_ ULONG Level,
    _In_ BOOLEAN State)
{
    PULONG ComponentMask;
    ULONG Mask;

    if (!SeSinglePrivilegeCheck(SeDebugPrivilege, ExGetPreviousMode()))
        return STATUS_ACCESS_DENIED;

    ComponentMask = KdpComponentMask(ComponentId);
    if (ComponentMask == NULL)
        return STATUS_INVALID_PARAMETER_1;

    Mask = KdpLevelToMask(Level);

    if (State)
        *ComponentMask |= Mask;
    else
        *ComponentMask &= ~Mask;

    return STATUS_SUCCESS;
}

/* EOF */
