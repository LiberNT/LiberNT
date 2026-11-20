/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Log the memory the firmware opened to Apple coprocessors through SART
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <arm64.h>
#include <Fdt.h>
#include <libernt/fdtlib.h>

#define APPLE_SART_MAX_ENTRIES      16
#define APPLE_SART_CONFIG(Index)    (0x00 + 4 * (Index))
#define APPLE_SART_PADDR(Index)     (0x40 + 4 * (Index))
#define APPLE_SART3_SIZE(Index)     (0x80 + 4 * (Index))
#define APPLE_SART2_CONFIG_SIZE     0x00FFFFFFUL
#define APPLE_SART2_CONFIG_FLAGS    0xFF000000UL
#define APPLE_SART_SHIFT            12
#define APPLE_SART2_REGISTER_SPAN   0x80
#define APPLE_SART3_REGISTER_SPAN   0xC0

static
VOID
Arm64RecordSart(
    _In_ ULONGLONG Address,
    _In_ BOOLEAN Version3)
{
    ULONGLONG Base, Size;
    ULONG Index, Config;

    for (Index = 0; Index < APPLE_SART_MAX_ENTRIES; ++Index)
    {
        Config = READ_REGISTER_ULONG((PULONG)(ULONG_PTR)(Address + APPLE_SART_CONFIG(Index)));
        Base = (ULONGLONG)READ_REGISTER_ULONG((PULONG)(ULONG_PTR)(Address + APPLE_SART_PADDR(Index))) << APPLE_SART_SHIFT;
        if (Version3)
        {
            Size = (ULONGLONG)READ_REGISTER_ULONG((PULONG)(ULONG_PTR)(Address + APPLE_SART3_SIZE(Index))) << APPLE_SART_SHIFT;
        }
        else
        {
            Size = (ULONGLONG)(Config & APPLE_SART2_CONFIG_SIZE) << APPLE_SART_SHIFT;
            Config &= APPLE_SART2_CONFIG_FLAGS;
        }
        if (!Config || !Size)
            continue;
        DbgPrint("winload: SART %I64x entry %lu keeps %I64x+%I64x for its coprocessor\n", Address, Index, Base, Size);
    }
}

static
VOID
Arm64ScanSart(
    _In_ PFDT Fdt,
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _In_ ULONG Depth)
{
    const VOID *Compatible;
    ULONGLONG Address, Size;
    ULONG Length, Child;
    BOOLEAN Version3;

    Compatible = FdtGetProperty(Fdt, Node, "compatible", &Length);
    if (Compatible && Parent != FDT_NO_NODE &&
        (FdtStringListContains(Compatible, Length, "apple,t6000-sart") ||
         FdtStringListContains(Compatible, Length, "apple,t8103-sart")))
    {
        Version3 = FdtStringListContains(Compatible, Length, "apple,t6000-sart");
        if (FdtReadReg(Fdt, Node, Parent, 0, &Address, &Size) && Address != 0 &&
            Size >= (Version3 ? APPLE_SART3_REGISTER_SPAN : APPLE_SART2_REGISTER_SPAN))
        {
            Arm64RecordSart(Address, Version3);
        }
        return;
    }
    if (Depth >= 8)
        return;
    for (Child = FdtFirstChild(Fdt, Node); Child != FDT_NO_NODE; Child = FdtNextSibling(Fdt, Child))
        Arm64ScanSart(Fdt, Child, Node, Depth + 1);
}

VOID
Arm64FindFirmwareSart(
    _In_ const VOID *DeviceTree,
    _In_ ULONG DeviceTreeSize)
{
    FDT Fdt;

    if (FdtOpen(DeviceTree, DeviceTreeSize, &Fdt))
        Arm64ScanSart(&Fdt, FdtRootNode(&Fdt), FDT_NO_NODE, 0);
}
