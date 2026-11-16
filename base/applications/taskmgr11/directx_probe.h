/*
 * PROJECT:     LiberNT Task Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hand-off of the DirectX version probe between the UI and its worker thread
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define TM_DXPROBE_FREE    0
#define TM_DXPROBE_QUEUED  1
#define TM_DXPROBE_RUNNING 2
#define TM_DXPROBE_DONE    3

typedef struct _TM_DXPROBE
{
    LUID Luid;
    WCHAR Text[48];
    volatile LONG State;
} TM_DXPROBE;

static inline BOOL TmDxProbeSameLuid(const LUID *first, const LUID *second)
{
    return first->LowPart == second->LowPart && first->HighPart == second->HighPart;
}

static inline BOOL TmDxProbeLuidPresent(const LUID *luid, const LUID *present, int presentCount)
{
    int i;

    for (i = 0; i < presentCount; i++)
    {
        if (TmDxProbeSameLuid(luid, &present[i]))
            return TRUE;
    }
    return FALSE;
}

static inline LONG TmDxProbeState(TM_DXPROBE *probe)
{
    return InterlockedCompareExchange(&probe->State, TM_DXPROBE_FREE, TM_DXPROBE_FREE);
}

static inline TM_DXPROBE *TmDxProbeQueue(TM_DXPROBE *probe, const LUID *luid)
{
    probe->Luid = *luid;
    probe->Text[0] = UNICODE_NULL;
    InterlockedExchange(&probe->State, TM_DXPROBE_QUEUED);
    return probe;
}

/* UI thread only: it rewrites FREE and DONE slots, the worker owns QUEUED and RUNNING ones */
static inline TM_DXPROBE *TmDxProbeLookup(TM_DXPROBE *probes, int count, const LUID *luid,
                                          const LUID *present, int presentCount)
{
    int i;

    for (i = 0; i < count; i++)
    {
        if (TmDxProbeState(&probes[i]) != TM_DXPROBE_FREE && TmDxProbeSameLuid(&probes[i].Luid, luid))
            return &probes[i];
    }
    for (i = 0; i < count; i++)
    {
        if (TmDxProbeState(&probes[i]) == TM_DXPROBE_FREE)
            return TmDxProbeQueue(&probes[i], luid);
    }
    for (i = 0; i < count; i++)
    {
        if (TmDxProbeState(&probes[i]) == TM_DXPROBE_DONE &&
            !TmDxProbeLuidPresent(&probes[i].Luid, present, presentCount))
        {
            return TmDxProbeQueue(&probes[i], luid);
        }
    }
    return NULL;
}

static inline BOOL TmDxProbeResult(TM_DXPROBE *probe, WCHAR *text, int cch)
{
    if (TmDxProbeState(probe) != TM_DXPROBE_DONE)
        return FALSE;
    lstrcpynW(text, probe->Text, cch);
    return TRUE;
}

static inline TM_DXPROBE *TmDxProbeClaim(TM_DXPROBE *probes, int count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        if (InterlockedCompareExchange(&probes[i].State, TM_DXPROBE_RUNNING, TM_DXPROBE_QUEUED) == TM_DXPROBE_QUEUED)
            return &probes[i];
    }
    return NULL;
}

static inline void TmDxProbeComplete(TM_DXPROBE *probe, const WCHAR *text)
{
    lstrcpynW(probe->Text, text, (int)(sizeof(probe->Text) / sizeof(probe->Text[0])));
    InterlockedExchange(&probe->State, TM_DXPROBE_DONE);
}
