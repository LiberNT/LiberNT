/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Host start of an Apple silicon Type-C DWC3 controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/appledwc3host.h>
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/appleatcphy.h>

#define DWC3_GCTL                   0xC110
#define DWC3_GCTL_PRTCAP_MASK       (3UL << 12)
#define DWC3_GCTL_PRTCAP_HOST       (1UL << 12)
#define DWC3_GSNPSID                0xC120
#define DWC3_GUSB2PHYCFG            0xC200
#define DWC3_GUSB2PHYCFG_SUSPHY     (1UL << 6)
#define DWC3_GUSB3PIPECTL           0xC2C0
#define DWC3_GUSB3PIPECTL_SUSPHY    (1UL << 17)
#define DWC3_DCTL                   0xC704
#define DWC3_DCTL_RUN_STOP          (1UL << 31)
#define DWC3_DCTL_CSFTRST           (1UL << 30)
#define DWC3_SOFT_RESET_POLLS       250
#define DWC3_SOFT_RESET_SETTLE_MS   50

#define APPLE_DWC3_CIO_LFPS_OFFSET  0x38
#define APPLE_DWC3_CIO_BW_NGT       0x3C
#define APPLE_DWC3_CIO_LINK_TIMER   0x40
#define APPLE_DWC3_GLUE_SPAN        0x44

C_ASSERT(DWC3_DCTL + sizeof(ULONG) <= APPLE_DWC3_REGISTER_SPAN);

static
ULONG
AppleDwc3Read(
    _In_ PUCHAR Core,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Core + Offset));
}

static
VOID
AppleDwc3Write(
    _In_ PUCHAR Core,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Core + Offset), Value);
}

static
NTSTATUS
AppleDwc3SoftReset(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ PUCHAR Core)
{
    ULONG Poll;

    AppleDwc3Write(Core, DWC3_DCTL, (AppleDwc3Read(Core, DWC3_DCTL) & ~DWC3_DCTL_RUN_STOP) | DWC3_DCTL_CSFTRST);
    for (Poll = 0; Poll < DWC3_SOFT_RESET_POLLS; ++Poll)
    {
        if (!(AppleDwc3Read(Core, DWC3_DCTL) & DWC3_DCTL_CSFTRST))
        {
            Tree->Io->Sleep(DWC3_SOFT_RESET_SETTLE_MS);
            return STATUS_SUCCESS;
        }
        Tree->Io->Sleep(1);
    }
    return STATUS_IO_TIMEOUT;
}

static
VOID
AppleDwc3SetupGlue(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG64 CoreBase,
    _In_z_ PCSTR Owner)
{
    PUCHAR Glue;
    ULONG Size, Index;

    Index = FdtDriverFindRegIndex(Tree, Node, "dwc3-apple");
    if (Index == MAXULONG)
        return;
    Glue = FdtDriverMapReg(Tree, Node, Index, NULL, &Size);
    if (!Glue)
        return;
    if (Size >= APPLE_DWC3_GLUE_SPAN)
    {
        AppleDwc3Write(Glue, APPLE_DWC3_CIO_LFPS_OFFSET, 0x0F800F80);
        AppleDwc3Write(Glue, APPLE_DWC3_CIO_BW_NGT, 0x0FC00FC0);
        AppleDwc3Write(Glue, APPLE_DWC3_CIO_LINK_TIMER,
                       (AppleDwc3Read(Glue, APPLE_DWC3_CIO_LINK_TIMER) & 0xFF000000UL) | 0x00140A10UL);
    }
    else
    {
        FdtDriverPrint(Tree, "%s: %I64x glue registers are only %lu bytes\n", Owner, CoreBase, Size);
    }
    Tree->Io->Unmap(Glue, Size);
}

NTSTATUS
NTAPI
AppleDwc3StartHost(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_ ULONG64 CoreBase,
    _In_z_ PCSTR Owner)
{
    APPLE_ATCPHY Phy;
    PUCHAR Core = NULL;
    ULONG Identifier, Control;
    NTSTATUS Status, PhyStatus;
    BOOLEAN HavePhy = FALSE;

    RtlZeroMemory(&Phy, sizeof(Phy));
    if (!Tree->Io)
        return STATUS_INVALID_PARAMETER;
    PhyStatus = AppleAtcphyOpen(Tree, Node, &Phy);
    if (NT_SUCCESS(PhyStatus))
    {
        HavePhy = TRUE;
        PhyStatus = ApplePmgrPowerOnDevice(Tree, Phy.Node);
        if (NT_SUCCESS(PhyStatus))
            PhyStatus = AppleAtcphyStartUsb2Host(Tree, &Phy);
        FdtDriverPrint(Tree, "%s: %I64x Type-C PHY USB2 host start %lx, tunables %lu/%lu/%lu\n", Owner, CoreBase,
                       PhyStatus, Phy.TunableEntries[0], Phy.TunableEntries[1], Phy.TunableEntries[2]);
        if (!NT_SUCCESS(PhyStatus))
        {
            Status = PhyStatus;
            goto Done;
        }
    }
    else if (PhyStatus != STATUS_NOT_FOUND)
    {
        FdtDriverPrint(Tree, "%s: %I64x Type-C PHY is not usable: %lx\n", Owner, CoreBase, PhyStatus);
    }
    Core = Tree->Io->Map(CoreBase, APPLE_DWC3_REGISTER_SPAN);
    if (!Core)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    Identifier = AppleDwc3Read(Core, DWC3_GSNPSID);
    Control = AppleDwc3Read(Core, DWC3_GCTL);
    FdtDriverPrint(Tree, "%s: %I64x core %08lx, control %08lx, USB2 PHY %08lx, USB3 pipe %08lx\n", Owner, CoreBase,
                   Identifier, Control, AppleDwc3Read(Core, DWC3_GUSB2PHYCFG), AppleDwc3Read(Core, DWC3_GUSB3PIPECTL));
    if ((Identifier & 0xFFFF0000UL) != 0x55330000UL && (Identifier & 0xFFFF0000UL) != 0x33310000UL &&
        (Identifier & 0xFFFF0000UL) != 0x33320000UL)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Done;
    }
    if (HavePhy)
    {
        Status = AppleDwc3SoftReset(Tree, Core);
        if (!NT_SUCCESS(Status))
        {
            FdtDriverPrint(Tree, "%s: %I64x core soft reset did not finish\n", Owner, CoreBase);
            goto Done;
        }
        AppleDwc3SetupGlue(Tree, Node, CoreBase, Owner);
        Control = AppleDwc3Read(Core, DWC3_GCTL);
    }
    AppleDwc3Write(Core, DWC3_GCTL, (Control & ~DWC3_GCTL_PRTCAP_MASK) | DWC3_GCTL_PRTCAP_HOST);
    if (HavePhy)
    {
        AppleDwc3Write(Core, DWC3_GUSB2PHYCFG, AppleDwc3Read(Core, DWC3_GUSB2PHYCFG) | DWC3_GUSB2PHYCFG_SUSPHY);
        AppleDwc3Write(Core, DWC3_GUSB3PIPECTL, AppleDwc3Read(Core, DWC3_GUSB3PIPECTL) | DWC3_GUSB3PIPECTL_SUSPHY);
        PhyStatus = AppleAtcphySelectDummyPipe(&Phy);
        FdtDriverPrint(Tree, "%s: %I64x USB3 pipe on the dummy PHY %lx, USB2 PHY %08lx, USB3 pipe %08lx\n", Owner,
                       CoreBase, PhyStatus, AppleDwc3Read(Core, DWC3_GUSB2PHYCFG),
                       AppleDwc3Read(Core, DWC3_GUSB3PIPECTL));
    }
    Status = STATUS_SUCCESS;

Done:
    if (Core)
        Tree->Io->Unmap(Core, APPLE_DWC3_REGISTER_SPAN);
    AppleAtcphyClose(&Phy);
    return Status;
}
