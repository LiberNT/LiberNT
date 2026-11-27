/*
 * PROJECT:     LiberNT DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hot-plug rebuild generation and worker-state transitions
 */

#ifndef _DXGK_HOTPLUG_WORK_CORE_H_
#define _DXGK_HOTPLUG_WORK_CORE_H_

#include <ntddk.h>

#define DXGK_HOTPLUG_MAX_TRANSIENT_RETRIES 6
#define DXGK_HOTPLUG_RETRY_BASE_MS 10
#define DXGK_HOTPLUG_RETRY_MAX_MS 100

/* The caller serializes generation, activation, and enumeration helpers with
 * one lock.  A successful activation owns rundown before the embedded work
 * item is queued; the retry-policy helpers are side-effect free. */
FORCEINLINE LONG64 DxgkHotPlugWorkCorePublishLocked(_Inout_ volatile LONG64 *Generation)
{
    return InterlockedIncrement64(Generation);
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreTryActivateLocked(_Inout_ volatile LONG *Active)
{
    if (*Active != 0)
        return FALSE;
    *Active = 1;
    return TRUE;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreCompleteLocked(_In_ volatile const LONG64 *Generation, _Inout_ volatile LONG *Active, _In_ LONG64 ObservedGeneration, _In_ BOOLEAN LifecycleOpen)
{
    ASSERT(*Active != 0);
    if (LifecycleOpen && *Generation != ObservedGeneration)
        return FALSE;
    *Active = 0;
    return TRUE;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreNeedsRebuildLocked(_In_ volatile const LONG64 *Generation, _In_ LONG64 RebuiltGeneration)
{
    ASSERT(*Generation >= RebuiltGeneration);
    return *Generation != RebuiltGeneration;
}

FORCEINLINE LONG64 DxgkHotPlugWorkCoreBeginEnumerationEpochLocked(_Inout_ volatile LONG64 *Epoch, _Inout_ volatile LONG *Enumerated)
{
    LONG64 NextEpoch = InterlockedIncrement64(Epoch);

    if (NextEpoch == 0)
        NextEpoch = InterlockedIncrement64(Epoch);
    InterlockedExchange(Enumerated, 0);
    return NextEpoch;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCorePublishEnumerationLocked(_In_ volatile const LONG64 *Epoch, _Inout_ volatile LONG *Enumerated, _In_ LONG64 ExpectedEpoch)
{
    if (*Epoch != ExpectedEpoch || *Enumerated != 0)
        return FALSE;
    *Enumerated = 1;
    return TRUE;
}

FORCEINLINE NTSTATUS DxgkHotPlugWorkCoreValidateEnumerationLocked(_In_ volatile const LONG64 *Epoch, _In_ volatile const LONG *Enumerated, _In_ LONG64 ExpectedEpoch)
{
    if (*Epoch != ExpectedEpoch)
        return STATUS_RETRY;
    if (*Enumerated == 0)
        return STATUS_DEVICE_NOT_READY;
    return STATUS_SUCCESS;
}

/* Which connected output carries the desktop: the one the current topology already drives, then one
 * with an EDID, then the one connected last.  A newly connected output joins the desktop; it does not
 * take the main display from the output showing it. */
FORCEINLINE BOOLEAN DxgkHotPlugWorkCorePrefersOutput(
    _In_ BOOLEAN CandidateDriven, _In_ BOOLEAN CandidateEdid, _In_ ULONG64 CandidateSequence,
    _In_ BOOLEAN CurrentDriven, _In_ BOOLEAN CurrentEdid, _In_ ULONG64 CurrentSequence)
{
    if (CandidateDriven != CurrentDriven)
        return CandidateDriven;
    if (CandidateEdid != CurrentEdid)
        return CandidateEdid;
    return CandidateSequence > CurrentSequence;
}

/* Path importance ordinals are unique within a topology (D3DKMDT_VIDPN_PRESENT_PATH_IMPORTANCE): 1 is primary */
FORCEINLINE ULONG DxgkHotPlugWorkCorePathImportance(_In_ SIZE_T PathIndex)
{
    ASSERT(PathIndex < 255);
    return (ULONG)(1 + PathIndex);
}

/* A further connected output extends the desktop on a source of its own; with no source left it clones source 0 */
FORCEINLINE ULONG DxgkHotPlugWorkCoreExtraPathSource(_In_ SIZE_T PathIndex, _In_ SIZE_T NumSources)
{
    ASSERT(PathIndex != 0);
    return PathIndex < NumSources ? (ULONG)PathIndex : 0;
}

/* A further source of an extended desktop scans out a primary of its own; the desktop source keeps the adapter's shared primary */
FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreSourceHasOwnPrimary(_In_ ULONG SourceId, _In_ ULONG DesktopSourceId, _In_ ULONG NumSources, _In_ ULONG MaxSourcePrimaries)
{
    return SourceId != 0 && SourceId != DesktopSourceId && SourceId < NumSources && SourceId < MaxSourcePrimaries;
}

/* A connector change is taken only by a worker that can drain it; before start completes it stays pending for the
 * start path to queue again. */
FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreTakeConnectorChange(_Inout_ volatile LONG *Pending, _In_ BOOLEAN AdapterStarted)
{
    if (!AdapterStarted)
        return FALSE;
    return InterlockedExchange(Pending, 0) != 0;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreCanAcquireLevel3AfterRundown(_In_ volatile const LONG *Active)
{
    return *Active == 0;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreIsTransientStatus(_In_ NTSTATUS Status)
{
    return Status == STATUS_RETRY || Status == STATUS_INSUFFICIENT_RESOURCES || Status == STATUS_NO_MEMORY || Status == STATUS_DEVICE_NOT_READY || Status == STATUS_DEVICE_BUSY || Status == STATUS_IO_TIMEOUT;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreAwaitsEnumerationLocked(_In_ volatile const LONG *Enumerated)
{
    return *Enumerated == 0;
}

FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreShouldRetry(_In_ NTSTATUS Status, _In_ ULONG RetryCount)
{
    return RetryCount < DXGK_HOTPLUG_MAX_TRANSIENT_RETRIES && DxgkHotPlugWorkCoreIsTransientStatus(Status);
}

FORCEINLINE ULONG DxgkHotPlugWorkCoreRetryDelayMs(_In_ ULONG RetryCount)
{
    ULONG DelayMs = DXGK_HOTPLUG_RETRY_BASE_MS;

    while (RetryCount-- != 0 && DelayMs < DXGK_HOTPLUG_RETRY_MAX_MS)
        DelayMs = min(DelayMs * 2, DXGK_HOTPLUG_RETRY_MAX_MS);
    return DelayMs;
}

/* A rebuild tells the session of a display change once per monitor connection, and otherwise only when the
 * committed paths or the desktop size changed; an adapter with no output left keeps its desktop mode silently. */
FORCEINLINE BOOLEAN DxgkHotPlugWorkCoreNotifiesDisplayChange(
    _In_ BOOLEAN VidPnCommitted,
    _In_ BOOLEAN MonitorArrived,
    _In_ BOOLEAN DesktopSized,
    _In_ BOOLEAN PathsChanged,
    _In_ BOOLEAN SizeChanged)
{
    return VidPnCommitted && (MonitorArrived || (DesktopSized && (PathsChanged || SizeChanged)));
}

#endif /* _DXGK_HOTPLUG_WORK_CORE_H_ */
