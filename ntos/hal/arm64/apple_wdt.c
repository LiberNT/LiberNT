/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple silicon system restart through the SoC watchdog
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ioaccess.h>
#include "halext.h"
#include "devtree.h"
#include "apple_aic.h"

#define NDEBUG
#include <debug.h>

#define APPLE_WDT_WD1_CUR_TIME      0x10
#define APPLE_WDT_WD1_BITE_TIME     0x14
#define APPLE_WDT_WD1_CTRL          0x1c
#define APPLE_WDT_CTRL_RESET_EN     0x4

static ULONG64 AppleWdtPhysical;
static ULONG64 AppleWdtBase;

static
VOID
AppleWdtRestart(VOID)
{
    ULONG Spin;

    if (!AppleWdtBase)
        return;
    *HalpMmio((ULONG_PTR)AppleWdtBase, APPLE_WDT_WD1_CTRL) = APPLE_WDT_CTRL_RESET_EN;
    *HalpMmio((ULONG_PTR)AppleWdtBase, APPLE_WDT_WD1_BITE_TIME) = 0;
    *HalpMmio((ULONG_PTR)AppleWdtBase, APPLE_WDT_WD1_CUR_TIME) = 0;
    (VOID)*HalpMmio((ULONG_PTR)AppleWdtBase, APPLE_WDT_WD1_CUR_TIME);
    for (Spin = 0; Spin < 50; Spin++)
        KeStallExecutionProcessor(1000);
}

static const HAL_ARM64_SYSTEM_RESET AppleWdtSystemReset =
{
    "Apple watchdog",
    AppleWdtRestart,
};

VOID
AppleWdtProbe(
    _In_ PFDT Fdt)
{
    static const PCSTR Compatibles[] = { "apple,t8103-wdt", "apple,wdt" };
    ULONG Node, Parent, Match;
    ULONG64 Length;

    if (!HalpArm64FindDeviceTreeNode(Fdt, Compatibles, RTL_NUMBER_OF(Compatibles), &Node, &Parent, &Match) ||
        !FdtReadReg(Fdt, Node, Parent, 0, &AppleWdtPhysical, &Length) ||
        Length < APPLE_WDT_WD1_CTRL + sizeof(ULONG))
    {
        AppleWdtPhysical = 0;
        DPRINT1("[WDT] no Apple watchdog in the device tree, restart will halt\n");
        return;
    }
    AppleWdtBase = AppleWdtPhysical;
    HalpArm64RegisterSystemReset(&AppleWdtSystemReset);
}

VOID
AppleWdtPhase1MapRuntimeMmio(VOID)
{
    PHYSICAL_ADDRESS Physical;
    PVOID Mapped;

    if (!AppleWdtPhysical)
        return;
    Physical.QuadPart = (LONGLONG)AppleWdtPhysical;
    Mapped = MmMapIoSpace(Physical, APPLE_WDT_WD1_CTRL + sizeof(ULONG), MmNonCached);
    if (!Mapped)
        DPRINT1("[WDT] cannot map the Apple watchdog at %I64x, restart will halt\n", AppleWdtPhysical);
    AppleWdtBase = (ULONG64)(ULONG_PTR)Mapped;
}
