/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     States of an NDIS Poll object between a request and the end of polling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NDIS6_POLL_H_
#define _NDIS6_POLL_H_

#define NDIS6_POLL_IDLE     0
#define NDIS6_POLL_QUEUED   1
#define NDIS6_POLL_RUNNING  2
#define NDIS6_POLL_RERUN    3

#define NDIS6_POLL_MAX_NBLS         64
#define NDIS6_POLL_MAX_ITERATIONS   16

FORCEINLINE
BOOLEAN
Ndis6PollCoreRequest(
    _In_ LONG State,
    _Out_ LONG *NewState)
{
    if (State == NDIS6_POLL_IDLE)
    {
        *NewState = NDIS6_POLL_QUEUED;
        return TRUE;
    }

    *NewState = (State == NDIS6_POLL_QUEUED) ? NDIS6_POLL_QUEUED : NDIS6_POLL_RERUN;
    return FALSE;
}

FORCEINLINE
BOOLEAN
Ndis6PollCoreFinish(
    _In_ LONG State,
    _Out_ LONG *NewState)
{
    if (State == NDIS6_POLL_RERUN)
    {
        *NewState = NDIS6_POLL_RUNNING;
        return TRUE;
    }

    *NewState = NDIS6_POLL_IDLE;
    return FALSE;
}

FORCEINLINE
BOOLEAN
Ndis6PollCoreMadeProgress(
    _In_ ULONG IndicatedNbls,
    _In_ ULONG CompletedNbls)
{
    return (BOOLEAN)(IndicatedNbls != 0 || CompletedNbls != 0);
}

#endif
