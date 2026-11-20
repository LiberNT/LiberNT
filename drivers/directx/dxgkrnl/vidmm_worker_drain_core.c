/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VidMm destroy-worker admission and drain transitions
 */

#include "vidmm_worker_drain_core.h"

BOOLEAN DxgkVidMmWorkerDrainCoreTryAdmitLocked(_Inout_ volatile LONG *ActiveWorkers, _In_ BOOLEAN AdmissionBlocked, _Out_ PBOOLEAN ResetDrainedEvent)
{
    if (ActiveWorkers == NULL || ResetDrainedEvent == NULL)
        return FALSE;
    *ResetDrainedEvent = FALSE;
    if (AdmissionBlocked || *ActiveWorkers < 0 || *ActiveWorkers == MAXLONG)
        return FALSE;
    *ResetDrainedEvent = *ActiveWorkers == 0;
    (*ActiveWorkers)++;
    ASSERT(*ActiveWorkers > 0);
    return TRUE;
}

BOOLEAN DxgkVidMmWorkerDrainCoreRetireLocked(_Inout_ volatile LONG *ActiveWorkers, _Out_ PBOOLEAN SetDrainedEvent)
{
    if (ActiveWorkers == NULL || SetDrainedEvent == NULL)
        return FALSE;
    *SetDrainedEvent = FALSE;
    if (*ActiveWorkers <= 0)
        return FALSE;
    (*ActiveWorkers)--;
    *SetDrainedEvent = *ActiveWorkers == 0;
    return TRUE;
}

BOOLEAN DxgkVidMmWorkerDrainCoreIsDrainedLocked(_In_ volatile const LONG *ActiveWorkers)
{
    return ActiveWorkers != NULL && *ActiveWorkers == 0;
}

VOID DxgkVidMmWorkerDrainCoreYield(_In_ PDXGK_VIDMM_WORKER_DRAIN_GATE Gate)
{
    BOOLEAN SetDrainedEvent;
    BOOLEAN Retired;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ExAcquireFastMutex(Gate->Lock);
    Retired = DxgkVidMmWorkerDrainCoreRetireLocked(Gate->ActiveWorkers, &SetDrainedEvent);
    ASSERT(Retired);
    if (Retired && SetDrainedEvent)
        KeSetEvent(Gate->DrainedEvent, IO_NO_INCREMENT, FALSE);
    ASSERT((KeReadStateEvent(Gate->DrainedEvent) != 0) == DxgkVidMmWorkerDrainCoreIsDrainedLocked(Gate->ActiveWorkers));
    ExReleaseFastMutex(Gate->Lock);
}

BOOLEAN DxgkVidMmWorkerDrainCoreReenter(_In_ PDXGK_VIDMM_WORKER_DRAIN_GATE Gate)
{
    BOOLEAN ResetDrainedEvent;
    BOOLEAN Admitted;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ExAcquireFastMutex(Gate->Lock);
    Admitted = DxgkVidMmWorkerDrainCoreTryAdmitLocked(Gate->ActiveWorkers,
                                                      InterlockedCompareExchange(Gate->AdmissionBlocked, 0, 0) != 0,
                                                      &ResetDrainedEvent);
    if (Admitted && ResetDrainedEvent)
        KeResetEvent(Gate->DrainedEvent);
    ASSERT((KeReadStateEvent(Gate->DrainedEvent) != 0) == DxgkVidMmWorkerDrainCoreIsDrainedLocked(Gate->ActiveWorkers));
    ExReleaseFastMutex(Gate->Lock);
    return Admitted;
}
