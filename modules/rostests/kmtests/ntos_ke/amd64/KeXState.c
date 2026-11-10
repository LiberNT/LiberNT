/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AMD64 extended state transitions and alignment
 */

#include <kmt_test.h>

START_TEST(KeXState)
{
    XSTATE_SAVE Outer, Inner;
    NTSTATUS OuterStatus, InnerStatus;
    KIRQL PreviousIrql;
    ULONG64 Mask;
    ULONG Index;
    int CpuInfo[4];

    Mask = RtlGetEnabledExtendedFeatures(XSTATE_MASK_LEGACY);
    if (Mask != 0)
    {
        OuterStatus = KeSaveExtendedProcessorState(Mask, &Outer);
        ok_eq_hex(OuterStatus, STATUS_SUCCESS);
        if (NT_SUCCESS(OuterStatus))
        {
            KeRaiseIrql(DISPATCH_LEVEL, &PreviousIrql);
            InnerStatus = KeSaveExtendedProcessorState(Mask, &Inner);
            if (NT_SUCCESS(InnerStatus))
                KeRestoreExtendedProcessorState(&Inner);
            KeLowerIrql(PreviousIrql);
            ok_eq_hex(InnerStatus, STATUS_SUCCESS);
            KeRestoreExtendedProcessorState(&Outer);
        }
    }

    if (!SharedUserData->XState.CompactionEnabled)
        return;

    for (Index = 2; Index < MAXIMUM_XSTATE_FEATURES; Index++)
    {
        if (!(SharedUserData->XState.EnabledFeatures & (1ULL << Index)))
            continue;
        __cpuidex(CpuInfo, 0xD, Index);
        if (CpuInfo[2] & 2)
            ok_eq_ulong(SharedUserData->XState.Features[Index].Offset & 63, 0);
    }
}
