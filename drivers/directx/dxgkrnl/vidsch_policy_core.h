/*
 * PROJECT:     LiberNT WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Testable video-scheduler policy helpers
 */

#ifndef _VIDSCH_POLICY_CORE_H_
#define _VIDSCH_POLICY_CORE_H_

#include <ntddk.h>

/*
 * The current scheduler has one public WDDM engine (ordinal zero) per node.
 * Its dxgmms2 queue array is flattened by node, so accepting another public
 * engine ordinal would alias two WDDM engines onto one queue.
 */
FORCEINLINE
BOOLEAN
VidSchPolicyNodeEngineSupported(
    _In_ ULONG NodeOrdinal,
    _In_ ULONG EngineOrdinal,
    _In_ ULONG NodeCount)
{
    return NodeOrdinal < NodeCount && EngineOrdinal == 0;
}

/* The nodes Windows 11 stops before resetting NodeOrdinal: its dependents on this adapter, not itself. */
FORCEINLINE
ULONGLONG
VidSchPolicyDependentNodes(
    _In_ ULONGLONG DependentNodeOrdinalMask,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG NodeCount)
{
    ULONGLONG Nodes = DependentNodeOrdinalMask;

    if (NodeOrdinal < 64)
        Nodes &= ~(1ULL << NodeOrdinal);
    if (NodeCount < 64)
        Nodes &= (1ULL << NodeCount) - 1;
    return Nodes;
}

/*
 * Context-owned work is cancelled as one ordered stream.  Contextless paging
 * and tracked packets still need a nonzero, stable owner, supplied by their
 * referenced device.
 */
FORCEINLINE
PVOID
VidSchPolicyOwnerCookie(
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Device)
{
    return Context != NULL ? Context : Device;
}

/* A packet completing after its device entered terminal page-fault state must
 * retire as failed; publishing it as success would signal work after the
 * faulting packet. */
FORCEINLINE
BOOLEAN
VidSchPolicyCompletionMustFail(
    _In_ LONG CurrentExecutionState,
    _In_ LONG PageFaultExecutionState)
{
    return CurrentExecutionState == PageFaultExecutionState;
}

/* Packet teardown may unpin GPU virtual addresses and take FAST_MUTEXes.
 * ExAcquireFastMutex is valid through APC_LEVEL, but never at the
 * DISPATCH_LEVEL used by the completion DPC. */
FORCEINLINE
BOOLEAN
VidSchPolicyPacketCleanupMustDefer(
    _In_ KIRQL CurrentIrql)
{
    return CurrentIrql > APC_LEVEL;
}

/* The engine only returns to idle once its whole queue drained, including the packet awaiting resubmission. */
FORCEINLINE
BOOLEAN
VidSchPolicyResubmissionAllowed(
    _In_ LONG EngineState,
    _In_ LONG IdleState,
    _In_ LONG RunningState)
{
    return EngineState == IdleState || EngineState == RunningState;
}

#endif /* _VIDSCH_POLICY_CORE_H_ */
