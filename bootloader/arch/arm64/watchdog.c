/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stop the Apple watchdog the firmware leaves running
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <arm64.h>
#include <Fdt.h>
#include <libernt/fdtlib.h>

#define APPLE_WDT_WD1_CUR_TIME   0x10
#define APPLE_WDT_WD1_BITE_TIME  0x14
#define APPLE_WDT_WD1_CTRL       0x1C
#define APPLE_WDT_REGISTER_SPAN  0x20
#define APPLE_WDT_CTRL_RESET_EN  (1U << 2)

static ULONGLONG Arm64FirmwareWatchdogBase;

static
BOOLEAN
Arm64FindAppleWatchdog(
    _In_ PFDT Fdt,
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _In_ ULONG Depth,
    _Out_ PULONG Found,
    _Out_ PULONG FoundParent)
{
    const VOID *Compatible;
    ULONG Length;
    ULONG Child;

    Compatible = FdtGetProperty(Fdt, Node, "compatible", &Length);
    if (Compatible && Parent != FDT_NO_NODE &&
        (FdtStringListContains(Compatible, Length, "apple,t8103-wdt") ||
         FdtStringListContains(Compatible, Length, "apple,wdt")))
    {
        *Found = Node;
        *FoundParent = Parent;
        return TRUE;
    }
    if (Depth >= 8)
        return FALSE;
    for (Child = FdtFirstChild(Fdt, Node); Child != FDT_NO_NODE; Child = FdtNextSibling(Fdt, Child))
    {
        if (Arm64FindAppleWatchdog(Fdt, Child, Node, Depth + 1, Found, FoundParent))
            return TRUE;
    }
    return FALSE;
}

VOID
Arm64FindFirmwareWatchdog(
    _In_ const VOID *DeviceTree,
    _In_ ULONG DeviceTreeSize)
{
    ULONGLONG Address;
    ULONGLONG Size;
    FDT Fdt;
    ULONG Parent;
    ULONG Node;
    ULONG Control;

    Arm64FirmwareWatchdogBase = 0;
    if (!FdtOpen(DeviceTree, DeviceTreeSize, &Fdt) ||
        !Arm64FindAppleWatchdog(&Fdt, FdtRootNode(&Fdt), FDT_NO_NODE, 0, &Node, &Parent) ||
        !FdtReadReg(&Fdt, Node, Parent, 0, &Address, &Size) ||
        Address == 0 || Size < APPLE_WDT_REGISTER_SPAN)
    {
        return;
    }

    Arm64FirmwareWatchdogBase = Address;
    Control = READ_REGISTER_ULONG((PULONG)(ULONG_PTR)(Address + APPLE_WDT_WD1_CTRL));
    DbgPrint("winload: Apple watchdog at %I64x: control %08lx, count %08lx of %08lx%s\n",
             Address,
             Control,
             READ_REGISTER_ULONG((PULONG)(ULONG_PTR)(Address + APPLE_WDT_WD1_CUR_TIME)),
             READ_REGISTER_ULONG((PULONG)(ULONG_PTR)(Address + APPLE_WDT_WD1_BITE_TIME)),
             (Control & APPLE_WDT_CTRL_RESET_EN) ? ", running: stopping it at the kernel handoff" : "");
}

VOID
Arm64StopFirmwareWatchdog(VOID)
{
    if (Arm64FirmwareWatchdogBase == 0)
        return;

    WRITE_REGISTER_ULONG((PULONG)(ULONG_PTR)(Arm64FirmwareWatchdogBase + APPLE_WDT_WD1_CTRL), 0);
    ARM64_DSB(SY);
}
