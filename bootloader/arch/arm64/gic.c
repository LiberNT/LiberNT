/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 Generic Interrupt Controller (GIC) stubs
 * COPYRIGHT:   Copyright 2024 Ahmed ARIF (contact@eotics.com)
 */

#include <bl.h>
#include <arm64.h>
DBG_DEFAULT_CHANNEL(WARNING);

/*
 * These are placeholder stubs to make the ARM64 interrupt path
 * explicit. They should be implemented for the target platform’s
 * GIC (usually GICv3 in AArch64 UEFI environments).
 */

VOID Arm64GicInitialize(VOID)
{
    /* Minimal CPU interface enable for GICv3 */
    ULONGLONG val;
    /* Priority mask: allow all priorities */
    val = 0xFF;
    ARM64_WRITE_SYSREG(ICC_PMR_EL1, val);
    /* Binary point: no preemption groups */
    val = 0;
    ARM64_WRITE_SYSREG(ICC_BPR1_EL1, val);
    /* Enable Group1 interrupts */
    val = 1;
    ARM64_WRITE_SYSREG(ICC_IGRPEN1_EL1, val);
    ARM64_ISB();
    TRACE("ARM64 GIC: CPU interface enabled (GICv3-style)\n");
}

ULONG Arm64GicGetVersion(VOID)
{
    /* Assume GICv3 in typical AArch64 UEFI environments */
    return 3;
}

ULONG Arm64GicAcknowledgeInterrupt(VOID)
{
    ULONGLONG iar;
    iar = ARM64_READ_SYSREG(ICC_IAR1_EL1);
    /* Spurious interrupt ID for GICv3 is 1023 */
    if ((iar & 0x3FF) == 1023)
        return (ULONG)~0U;
    return (ULONG)(iar & 0xFFFFFF);
}

VOID Arm64GicEndOfInterrupt(ULONG IntId)
{
    ULONGLONG eoi = (ULONGLONG)IntId;
    ARM64_WRITE_SYSREG(ICC_EOIR1_EL1, eoi);
}

VOID Arm64GicEnableInterrupt(ULONG IntId)
{
    /* Requires GIC distributor base; not configured in FreeLoader. */
    (void)IntId;
}

VOID Arm64GicDisableInterrupt(ULONG IntId)
{
    (void)IntId;
}

VOID Arm64GicSetPriorityMask(UCHAR Priority)
{
    ULONGLONG val = (ULONGLONG)Priority;
    ARM64_WRITE_SYSREG(ICC_PMR_EL1, val);
    ARM64_ISB();
}

VOID Arm64GicSendSGI(UCHAR SgiId, UCHAR TargetList, BOOLEAN ToAll)
{
    /* Without MPIDR routing details, we cannot reliably send SGIs. */
    (void)SgiId; (void)TargetList; (void)ToAll;
}
