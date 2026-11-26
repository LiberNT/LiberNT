/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WDDM scheduler and SoftGPU terminal fault policy tests
 */

#include <kmt_test.h>
#include "vidsch_policy_core.h"
#include "fault_policy_core.h"

static VOID
TestCompletionAlreadyRetired(VOID)
{
    ok_bool_true(VidSchPolicyCompletionAlreadyRetired(FALSE, 40542, 40542), "repeated latest fence");
    ok_bool_true(VidSchPolicyCompletionAlreadyRetired(FALSE, 40500, 40542), "older fence");
    ok_bool_false(VidSchPolicyCompletionAlreadyRetired(FALSE, 40543, 40542), "next fence");
    ok_bool_false(VidSchPolicyCompletionAlreadyRetired(TRUE, 40542, 40542), "preemption");
    ok_bool_false(VidSchPolicyCompletionAlreadyRetired(FALSE, 0, 40542), "fence zero");
    ok_bool_false(VidSchPolicyCompletionAlreadyRetired(FALSE, 5, 0), "nothing completed");
    ok_bool_true(VidSchPolicyCompletionAlreadyRetired(FALSE, 0xFFFFFFF0, 0x10), "wrapped completion");
    ok_bool_false(VidSchPolicyCompletionAlreadyRetired(FALSE, 0x10, 0xFFFFFFF0), "wrapped next fence");
}

static VOID
TestFenceAbortedByReset(VOID)
{
    ok_bool_true(VidSchPolicyFenceAbortedByReset(249, 248, 249), "reset boundary after the last completion");
    ok_bool_true(VidSchPolicyFenceAbortedByReset(250, 248, 255), "aborted fence inside the boundary");
    ok_bool_true(VidSchPolicyFenceAbortedByReset(5, 0, 7), "nothing completed before the reset");
    ok_bool_false(VidSchPolicyFenceAbortedByReset(248, 248, 249), "last successful completion");
    ok_bool_false(VidSchPolicyFenceAbortedByReset(200, 248, 249), "older completed fence");
    ok_bool_false(VidSchPolicyFenceAbortedByReset(250, 248, 249), "fence past the boundary");
    ok_bool_false(VidSchPolicyFenceAbortedByReset(249, 248, 0), "no reset");
    ok_bool_false(VidSchPolicyFenceAbortedByReset(0, 248, 249), "fence zero");
    ok_bool_false(VidSchPolicyFenceAbortedByReset(249, 249, 249), "boundary already completed");
    ok_bool_true(VidSchPolicyFenceAbortedByReset(0x10, 0xFFFFFFF0, 0x20), "wrapped aborted fence");
    ok_bool_false(VidSchPolicyFenceAbortedByReset(0x30, 0xFFFFFFF0, 0x20), "wrapped fence past the boundary");
}

static VOID
TestPreemptCommandIrql(VOID)
{
    ok_eq_uint(VidSchPolicyPreemptCommandIrql(FALSE), DISPATCH_LEVEL);
    ok_eq_uint(VidSchPolicyPreemptCommandIrql(TRUE), PASSIVE_LEVEL);
}

START_TEST(DxgkGpuFaultPolicy)
{
    PVOID Context = (PVOID)(ULONG_PTR)0x1111;
    PVOID Device = (PVOID)(ULONG_PTR)0x2222;
    SOFTGPU_FAULT_RETIREMENT Retirement;

    TestCompletionAlreadyRetired();
    TestFenceAbortedByReset();
    TestPreemptCommandIrql();
    ok(VidSchPolicyDependentNodes(0, 0, 4) == 0, "no dependents\n");
    ok(VidSchPolicyDependentNodes(0x1, 0, 4) == 0, "the reset node is not its own dependent\n");
    ok(VidSchPolicyDependentNodes(0x7, 1, 4) == 0x5, "dependents without the reset node\n");
    ok(VidSchPolicyDependentNodes(0xF0, 0, 4) == 0, "mask bits past the node count are ignored\n");
    ok(VidSchPolicyDependentNodes(0xFFFFFFFFFFFFFFFFULL, 63, 64) == 0x7FFFFFFFFFFFFFFFULL, "the full 64-node mask keeps its nodes\n");

    /* Node and engine are distinct WDDM coordinates. */
    ok_bool_true(
        VidSchPolicyNodeEngineSupported(0, 0, 2),
        "node 0 engine 0");
    ok_bool_true(
        VidSchPolicyNodeEngineSupported(1, 0, 2),
        "node 1 engine 0");
    ok_bool_false(
        VidSchPolicyNodeEngineSupported(2, 0, 2),
        "node out of range");
    ok_bool_false(
        VidSchPolicyNodeEngineSupported(0, 1, 2),
        "nonzero engine must not alias node 1");

    ok_eq_pointer(
        VidSchPolicyOwnerCookie(Context, Device),
        Context);
    ok_eq_pointer(
        VidSchPolicyOwnerCookie(NULL, Device),
        Device);
    ok_eq_pointer(
        VidSchPolicyOwnerCookie(NULL, NULL),
        NULL);

    ok_bool_true(
        VidSchPolicyCompletionMustFail(4, 4),
        "terminal page-fault state rejects completion");
    ok_bool_false(
        VidSchPolicyCompletionMustFail(0, 4),
        "active device accepts completion");

    ok_bool_false(
        VidSchPolicyPacketCleanupMustDefer(PASSIVE_LEVEL),
        "PASSIVE_LEVEL packet cleanup can run inline");
    ok_bool_false(
        VidSchPolicyPacketCleanupMustDefer(APC_LEVEL),
        "APC_LEVEL is the FAST_MUTEX ceiling");
    ok_bool_true(
        VidSchPolicyPacketCleanupMustDefer(DISPATCH_LEVEL),
        "completion-DPC packet cleanup must be deferred");

    ok_bool_true(
        VidSchPolicyIgnoreEngineInterrupt(6, 6),
        "an engine being reset has its completion and preemption interrupts ignored");
    ok_bool_false(
        VidSchPolicyIgnoreEngineInterrupt(1, 6),
        "a running engine's interrupts are processed");
    ok_bool_false(
        VidSchPolicyIgnoreEngineInterrupt(0, 6),
        "an idle engine's interrupts are processed");

    ok_bool_true(VidSchPolicyResetAbortedFence(5, 4, 6), "a fence after the last completed one is aborted");
    ok_bool_true(VidSchPolicyResetAbortedFence(6, 4, 6), "the last aborted fence itself is aborted");
    ok_bool_false(VidSchPolicyResetAbortedFence(4, 4, 6), "the last completed fence is not aborted");
    ok_bool_false(VidSchPolicyResetAbortedFence(7, 4, 6), "a fence after the last aborted one survives the reset");
    ok_bool_false(VidSchPolicyResetAbortedFence(4, 4, 4), "nothing is aborted when the hardware queue was empty");
    ok_bool_true(VidSchPolicyResetAbortedFence(1, 0xFFFFFFFEu, 2), "the aborted range crosses the 32-bit wrap");

    ok_bool_true(VidSchPolicyResetHoldsMiniport(TRUE, Device, Context), "a recovery on another thread holds the miniport");
    ok_bool_true(VidSchPolicyResetHoldsMiniport(TRUE, NULL, Context), "a recovery still draining calls holds the miniport");
    ok_bool_false(VidSchPolicyResetHoldsMiniport(TRUE, Context, Context), "the recovering thread itself is not held off");
    ok_bool_false(VidSchPolicyResetHoldsMiniport(FALSE, Device, Context), "an exclusive owner outside a recovery holds nothing");
    ok_bool_false(VidSchPolicyResetHoldsMiniport(FALSE, NULL, Context), "nothing holds the miniport outside a recovery");

    ok_bool_true(
        VidSchPolicyResubmissionAllowed(0, 0, 1),
        "an idle engine accepts a handed-back packet");
    ok_bool_true(
        VidSchPolicyResubmissionAllowed(1, 0, 1),
        "a running engine accepts a handed-back packet");
    ok_bool_false(
        VidSchPolicyResubmissionAllowed(6, 0, 1),
        "a resetting engine does not");

    ok_bool_true(
        SoftGpuDmaCommandAddressClassAllowed(
            FALSE,
            SoftGpuDmaAddressPhysicalSurface),
        "patched physical submission can execute a surface command");
    ok_bool_true(
        SoftGpuDmaCommandAddressClassAllowed(
            FALSE,
            SoftGpuDmaAddressKernelPaging),
        "KMD physical submission can execute a paging command");
    ok_bool_true(
        SoftGpuDmaCommandAddressClassAllowed(
            TRUE,
            SoftGpuDmaAddressGpuVaChecked),
        "virtual submission can execute an explicitly GPUVA-checked command");
    ok_bool_false(
        SoftGpuDmaCommandAddressClassAllowed(
            TRUE,
            SoftGpuDmaAddressPhysicalSurface),
        "virtual submission cannot reinterpret a GPUVA as a slab address");
    ok_bool_false(
        SoftGpuDmaCommandAddressClassAllowed(
            TRUE,
            SoftGpuDmaAddressKernelPaging),
        "process command buffer cannot supply a kernel paging address");
    ok_bool_false(
        SoftGpuDmaCommandAddressClassAllowed(
            TRUE,
            (SOFTGPU_DMA_ADDRESS_CLASS)99),
        "unknown command address class");

    SoftGpuFaultPolicyRetire(1300, 7, 12, 10, &Retirement);
    ok_eq_ulong(Retirement.NextHead, 8);
    ok_eq_ulong(Retirement.CompletedFence, 12);
    ok_bool_false(
        Retirement.NotifyPageFault,
        "WDDM 1.x consumes a fault as a no-op completion");

    /* Signed fence comparison must preserve wrap-around ordering. */
    SoftGpuFaultPolicyRetire(
        1999,
        MAXULONG,
        1,
        MAXULONG - 1,
        &Retirement);
    ok_eq_ulong(Retirement.NextHead, 0);
    ok_eq_ulong(Retirement.CompletedFence, 1);
    ok_bool_false(
        Retirement.NotifyPageFault,
        "pre-2.0 target keeps forward progress");

    SoftGpuFaultPolicyRetire(2000, 9, 15, 12, &Retirement);
    ok_eq_ulong(Retirement.NextHead, 10);
    ok_eq_ulong(Retirement.CompletedFence, 12);
    ok_bool_true(
        Retirement.NotifyPageFault,
        "WDDM 2.0 reports the fault without signaling its fence");

    SoftGpuFaultPolicyRetire(3000, 3, 25, 20, &Retirement);
    ok_eq_ulong(Retirement.NextHead, 4);
    ok_eq_ulong(Retirement.CompletedFence, 20);
    ok_bool_true(
        Retirement.NotifyPageFault,
        "WDDM 3.0 keeps the WDDM 2.0 fault contract");
}
