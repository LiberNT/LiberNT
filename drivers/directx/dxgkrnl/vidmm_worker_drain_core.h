/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     VidMm destroy-worker admission and drain transitions
 */

#ifndef _DXGK_VIDMM_WORKER_DRAIN_CORE_H_
#define _DXGK_VIDMM_WORKER_DRAIN_CORE_H_

#include <ntddk.h>

/* The caller must serialize each transition, the admission-blocked state,
 * and the drained event with the same lock.  Event changes returned by these
 * helpers must be applied before that lock is released. */
BOOLEAN DxgkVidMmWorkerDrainCoreTryAdmitLocked(_Inout_ volatile LONG *ActiveWorkers, _In_ BOOLEAN AdmissionBlocked, _Out_ PBOOLEAN ResetDrainedEvent);
BOOLEAN DxgkVidMmWorkerDrainCoreRetireLocked(_Inout_ volatile LONG *ActiveWorkers, _Out_ PBOOLEAN SetDrainedEvent);
BOOLEAN DxgkVidMmWorkerDrainCoreIsDrainedLocked(_In_ volatile const LONG *ActiveWorkers);

typedef struct _DXGK_VIDMM_WORKER_DRAIN_GATE
{
    PFAST_MUTEX Lock;
    volatile LONG *ActiveWorkers;
    volatile LONG *AdmissionBlocked;
    PKEVENT DrainedEvent;
} DXGK_VIDMM_WORKER_DRAIN_GATE, *PDXGK_VIDMM_WORKER_DRAIN_GATE;

/* A counted worker gives its count back before it waits on an owner that may
 * be draining the workers, and goes on afterwards only if admission reopened. */
VOID DxgkVidMmWorkerDrainCoreYield(_In_ PDXGK_VIDMM_WORKER_DRAIN_GATE Gate);
BOOLEAN DxgkVidMmWorkerDrainCoreReenter(_In_ PDXGK_VIDMM_WORKER_DRAIN_GATE Gate);

#endif /* _DXGK_VIDMM_WORKER_DRAIN_CORE_H_ */
