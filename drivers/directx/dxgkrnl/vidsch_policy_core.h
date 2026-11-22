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

/* Between the reset snapshot and the end of DxgkDdiResetEngine, completion and preemption interrupts from the timed-out engine are ignored. */
FORCEINLINE
BOOLEAN
VidSchPolicyIgnoreEngineInterrupt(
    _In_ LONG EngineState,
    _In_ LONG ResettingState)
{
    return EngineState == ResettingState;
}

/* A DMA completion naming a fence the node already completed carries no new completion: the miniport reports the
 * latest completed fence, which it may repeat. */
FORCEINLINE
BOOLEAN
VidSchPolicyCompletionAlreadyRetired(
    _In_ BOOLEAN PreemptionNotification,
    _In_ ULONG FenceId,
    _In_ ULONG LastCompletedFenceId)
{
    return !PreemptionNotification && FenceId != 0 && LastCompletedFenceId != 0 &&
           (LONG)(LastCompletedFenceId - FenceId) >= 0;
}

/* A packet an engine reset aborted: after the last completed fence and up to the last aborted one, in signed 32-bit fence distance. */
FORCEINLINE
BOOLEAN
VidSchPolicyResetAbortedFence(
    _In_ ULONG FenceId,
    _In_ ULONG LastCompletedFenceId,
    _In_ ULONG LastAbortedFenceId)
{
    return (LONG)(FenceId - LastCompletedFenceId) > 0 && (LONG)(LastAbortedFenceId - FenceId) >= 0;
}

/* From timeout detection to the end of recovery, while another thread holds the miniport, display queries answer from the state the reset leaves unchanged. */
FORCEINLINE
BOOLEAN
VidSchPolicyResetHoldsMiniport(
    _In_ BOOLEAN RecoveryActive,
    _In_opt_ PVOID ResetOwner,
    _In_ PVOID Caller)
{
    return RecoveryActive && ResetOwner != Caller;
}

/* DxgkDdiPreemptCommand runs at DISPATCH_LEVEL unless the driver set DXGK_VIDSCHCAPS.LowIrqlPreemptCommand */
FORCEINLINE
KIRQL
VidSchPolicyPreemptCommandIrql(
    _In_ BOOLEAN LowIrqlPreemptCommand)
{
    return LowIrqlPreemptCommand ? PASSIVE_LEVEL : DISPATCH_LEVEL;
}

#endif /* _VIDSCH_POLICY_CORE_H_ */
