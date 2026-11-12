/*
 * PROJECT:     LiberNT Apple RTKit Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Port and link bring-up of the Apple silicon PCIe controller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef UNIT_TEST
#include <ntddk.h>
#endif
#include <reactos/drivers/applepcie.h>
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/appledart.h>

#define PHY_LANE_CFG                0x00
#define PHY_LANE_CFG_REFCLK0REQ     (1UL << 0)
#define PHY_LANE_CFG_REFCLK1REQ     (1UL << 1)
#define PHY_LANE_CFG_REFCLK0ACK     (1UL << 2)
#define PHY_LANE_CFG_REFCLK1ACK     (1UL << 3)
#define PHY_LANE_CFG_REFCLKEN       (3UL << 9)
#define PHY_LANE_CFG_REFCLKCGEN     (3UL << 30)

#define PORT_LTSSMCTL               0x0080
#define PORT_LTSSMCTL_START         (1UL << 0)
#define PORT_INTSTAT                0x0100
#define PORT_INTMSK                 0x0104
#define PORT_MSICFG                 0x0124
#define PORT_MSICFG_EN              (1UL << 0)
#define PORT_MSIADDR                0x016C
#define PORT_MSIADDR_HI             0x0170
#define PORT_LINKSTS                0x0208
#define PORT_LINKSTS_UP             (1UL << 0)
#define PORT_LINKCMDSTS             0x0210
#define PORT_APPCLK                 0x0800
#define PORT_APPCLK_EN              (1UL << 0)
#define PORT_APPCLK_CGDIS           (1UL << 8)
#define PORT_STATUS                 0x0804
#define PORT_STATUS_READY           (1UL << 0)
#define PORT_PERST                  0x082C
#define PORT_PERST_OFF              (1UL << 0)
#define PORT_RID2SID                0x3000
#define PORT_RID2SID_MAX            512
#define PORT_MSIMAP                 0x3800
#define PORT_MSIMAP_ENABLE          (1UL << 31)
#define PORT_SPAN                   (PORT_MSIMAP + 4 * 32)
#define PORT_RID2SID_PROBE          0xBAD1DUL
#define PORT_RID2SID_VALID          (1UL << 31)

#define PCIE_MAX_MSI_VECTORS        32

#define PCIE_CFG_PAGE               0x1000
#define PCIE_CFG_MAX_FUNCTIONS      APPLE_PCIE_LINK_MAX_FUNCTIONS
#define PCIE_CFG_STATUS             0x04
#define PCIE_CFG_STATUS_CAPS        (1UL << 20)
#define PCIE_CFG_CAPABILITIES       0x34
#define PCIE_CFG_HEADER_END         0x40
#define PCIE_CFG_CAP_EXPRESS        0x10
#define PCIE_CFG_CAP_WALK_LIMIT     48
#define PCIE_CFG_EXTENDED           0x100
#define PCIE_CFG_EXT_WALK_LIMIT     256
#define PCIE_CFG_EXT_LTR            0x0018
#define PCIE_CFG_EXT_L1_PM_SS       0x001E
#define PCIE_EXP_DEVICE_CAPS        0x04
#define PCIE_EXP_LINK_CAPS          0x0C
#define PCIE_EXP_LINK_CONTROL       0x10
#define PCIE_EXP_DEVICE_CAPS2       0x24
#define PCIE_EXP_DEVICE_CONTROL2    0x28
#define PCIE_EXP_VERSION(Header)    (((Header) >> 16) & 0xF)
#define PCIE_DEVCAP2_LTR            (1UL << 11)
#define PCIE_DEVCTL2_LTR            (1UL << 10)
#define PCIE_LTR_MAX_LATENCY        0x04
#define PCIE_L1SS_CAPABILITIES      0x04
#define PCIE_L1SS_CONTROL1          0x08
#define PCIE_L1SS_CONTROL2          0x0C

#define PIN_DATA                    (1UL << 0)
#define PIN_MODE                    (7UL << 1)
#define PIN_MODE_OUTPUT             (1UL << 1)
#define PIN_PERIPH                  (3UL << 5)
#define PIN_INPUT_ENABLE            (1UL << 9)

#define PCIE_REFCLK_STEP_US         100
#define PCIE_REFCLK_TIMEOUT_US      50000
#define PCIE_TPERST_CLK_MS          1
#define PCIE_TPVPERL_MS             100
#define PCIE_PERST_SETTLE_MS        100
#define PCIE_READY_TIMEOUT_MS       250
#define PCIE_LINK_TIMEOUT_MS        500

#ifndef UNIT_TEST
static
VOID
PcieSleepMs(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}
#endif

static
ULONG
PcieElapsedMs(
    _In_ ULONG64 Start)
{
    return (ULONG)((KeQueryInterruptTime() - Start) / 10000);
}

static
ULONG
PcieRead(
    _In_ PAPPLE_PCIE_REGION Region,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Region->Base + Offset));
}

static
VOID
PcieWrite(
    _In_ PAPPLE_PCIE_REGION Region,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Region->Base + Offset), Value);
}

static
VOID
PcieMask(
    _In_ PAPPLE_PCIE_REGION Region,
    _In_ ULONG Offset,
    _In_ ULONG Clear,
    _In_ ULONG Set)
{
    PcieWrite(Region, Offset, (PcieRead(Region, Offset) & ~Clear) | Set);
}

static
BOOLEAN
PcieMapNamed(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _In_ ULONG MinimumSize,
    _Out_ PAPPLE_PCIE_REGION Region)
{
    ULONG Index = FdtDriverFindRegIndex(Tree, Node, Name);

    Region->Base = NULL;
    Region->Size = 0;
    if (Index == MAXULONG)
        return FALSE;
    Region->Base = FdtDriverMapReg(Tree, Node, Index, NULL, &Region->Size);
    if (Region->Base && Region->Size < MinimumSize)
    {
        MmUnmapIoSpace(Region->Base, Region->Size);
        Region->Base = NULL;
        Region->Size = 0;
    }
    return Region->Base != NULL;
}

static
VOID
PcieUnmap(
    _Inout_ PAPPLE_PCIE_REGION Region)
{
    if (Region->Base)
        MmUnmapIoSpace(Region->Base, Region->Size);
    Region->Base = NULL;
    Region->Size = 0;
}

static
BOOLEAN
PcieNodeEnabled(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node)
{
    const CHAR *Status;
    ULONG Length;

    Status = FdtGetProperty(&Tree->Fdt, Node, "status", &Length);
    if (!Status)
        return TRUE;
    return (Length == sizeof("okay") && !memcmp(Status, "okay", sizeof("okay"))) ||
           (Length == sizeof("ok") && !memcmp(Status, "ok", sizeof("ok")));
}

static
BOOLEAN
PcieIsPciNode(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node)
{
    const CHAR *Type;
    ULONG Length;

    Type = FdtGetProperty(&Tree->Fdt, Node, "device_type", &Length);
    return Type && Length == sizeof("pci") && !memcmp(Type, "pci", sizeof("pci"));
}

static
VOID
PcieApplyPinmux(
    _In_ PFDT_DRIVER_TREE Tree,
    _Inout_ PAPPLE_PCIE Pcie)
{
    const UCHAR *Phandles, *Pinmux;
    ULONG Length, PinmuxLength, Group, Controller, Index, Entry, Pin;

    Phandles = FdtGetProperty(&Tree->Fdt, Pcie->Node, "pinctrl-0", &Length);
    if (!Phandles || Length < sizeof(ULONG))
        return;
    Group = FdtDriverFindPhandle(Tree, FdtReadBigEndian32(Phandles));
    if (Group == FDT_NO_NODE)
        return;
    Controller = FdtDriverFindParentNode(Tree, Group);
    if (Controller == FDT_NO_NODE || Controller != Pcie->PinctrlNode || !Pcie->Pinctrl.Base)
        return;
    Pinmux = FdtGetProperty(&Tree->Fdt, Group, "pinmux", &PinmuxLength);
    for (Index = 0; Pinmux && Index + sizeof(ULONG) <= PinmuxLength; Index += sizeof(ULONG))
    {
        Entry = FdtReadBigEndian32(Pinmux + Index);
        Pin = Entry & 0xFFFF;
        if ((Pin + 1) * sizeof(ULONG) > Pcie->Pinctrl.Size || (Entry >> 16) > 3)
            continue;
        PcieMask(&Pcie->Pinctrl, Pin * sizeof(ULONG), PIN_PERIPH | PIN_INPUT_ENABLE,
                 ((Entry >> 16) << 5) | PIN_INPUT_ENABLE);
    }
}

static
BOOLEAN
PcieReadGpio(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _In_z_ PCSTR Property,
    _Out_ PULONG Controller,
    _Out_ PULONG Pin,
    _Out_ PBOOLEAN ActiveLow)
{
    const UCHAR *Spec;
    ULONG Length;

    Spec = FdtGetProperty(&Tree->Fdt, Node, Property, &Length);
    if (!Spec || Length < 3 * sizeof(ULONG))
        return FALSE;
    *Controller = FdtDriverFindPhandle(Tree, FdtReadBigEndian32(Spec));
    *Pin = FdtReadBigEndian32(Spec + 4);
    *ActiveLow = (FdtReadBigEndian32(Spec + 8) & 1) != 0;
    return *Controller != FDT_NO_NODE;
}

static
VOID
PcieSetResetPin(
    _Inout_ PAPPLE_PCIE Pcie,
    _In_ ULONG Pin,
    _In_ BOOLEAN ActiveLow,
    _In_ BOOLEAN Asserted)
{
    BOOLEAN High = Asserted ? !ActiveLow : ActiveLow;

    PcieMask(&Pcie->Pinctrl, Pin * sizeof(ULONG), PIN_PERIPH | PIN_MODE | PIN_DATA,
             PIN_MODE_OUTPUT | (High ? PIN_DATA : 0));
}

static
BOOLEAN
PcieWaitPhy(
    _In_ PAPPLE_PCIE_PORT Port,
    _In_ ULONG Ack)
{
    ULONG Waited;

    for (Waited = 0;; Waited += PCIE_REFCLK_STEP_US)
    {
        if (PcieRead(&Port->Phy, PHY_LANE_CFG) & Ack)
            return TRUE;
        if (Waited >= PCIE_REFCLK_TIMEOUT_US)
            return FALSE;
        KeStallExecutionProcessor(PCIE_REFCLK_STEP_US);
    }
}

static
NTSTATUS
PcieBringUpLink(
    _In_ PFDT_DRIVER_TREE Tree,
    _Inout_ PAPPLE_PCIE Pcie,
    _Inout_ PAPPLE_PCIE_PORT Port,
    _In_ BOOLEAN PowerCycle)
{
    ULONG ResetController, ResetPin, PwrenController = FDT_NO_NODE, PwrenPin = 0, Length;
    BOOLEAN ResetActiveLow, PwrenActiveLow = FALSE, Pwren;
    ULONG64 ReadyStart;
    const VOID *Compatible;
    NTSTATUS Status;

    if (!PcieReadGpio(Tree, Port->Node, "reset-gpios", &ResetController, &ResetPin, &ResetActiveLow) ||
        ResetController != Pcie->PinctrlNode || !Pcie->Pinctrl.Base ||
        (ResetPin + 1) * sizeof(ULONG) > Pcie->Pinctrl.Size)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Pwren = PcieReadGpio(Tree, Port->Node, "pwren-gpios", &PwrenController, &PwrenPin, &PwrenActiveLow);

    PcieMask(&Port->Registers, PORT_APPCLK, 0, PORT_APPCLK_EN);
    PcieSetResetPin(Pcie, ResetPin, ResetActiveLow, TRUE);
    if (Pwren)
    {
        Compatible = FdtGetProperty(&Tree->Fdt, PwrenController, "compatible", &Length);
        if (!Pcie->SetSmcGpio ||
            (!FdtStringListContains(Compatible, Length, "apple,smc-gpio") &&
             !FdtStringListContains(Compatible, Length, "apple,smc-low-gpio")))
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (PowerCycle)
        {
            Status = Pcie->SetSmcGpio(Pcie->SmcGpioContext,
                                      FdtStringListContains(Compatible, Length, "apple,smc-low-gpio"),
                                      (UCHAR)PwrenPin, PwrenActiveLow);
            if (!NT_SUCCESS(Status))
                return Status;
            PcieSleepMs(PCIE_TPVPERL_MS);
        }
        Status = Pcie->SetSmcGpio(Pcie->SmcGpioContext,
                                  FdtStringListContains(Compatible, Length, "apple,smc-low-gpio"),
                                  (UCHAR)PwrenPin, !PwrenActiveLow);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    PcieMask(&Port->Phy, PHY_LANE_CFG, 0, PHY_LANE_CFG_REFCLK0REQ);
    if (!PcieWaitPhy(Port, PHY_LANE_CFG_REFCLK0ACK))
        return STATUS_IO_TIMEOUT;
    PcieMask(&Port->Phy, PHY_LANE_CFG, 0, PHY_LANE_CFG_REFCLK1REQ);
    if (!PcieWaitPhy(Port, PHY_LANE_CFG_REFCLK1ACK))
        return STATUS_IO_TIMEOUT;
    PcieMask(&Port->Phy, PHY_LANE_CFG, 0, PHY_LANE_CFG_REFCLKEN);
    PcieSleepMs(Pwren ? PCIE_TPVPERL_MS : PCIE_TPERST_CLK_MS);

    PcieMask(&Port->Registers, PORT_PERST, 0, PORT_PERST_OFF);
    PcieSetResetPin(Pcie, ResetPin, ResetActiveLow, FALSE);
    PcieSleepMs(PCIE_PERST_SETTLE_MS);
    ReadyStart = KeQueryInterruptTime();
    while (!(PcieRead(&Port->Registers, PORT_STATUS) & PORT_STATUS_READY))
    {
        if (PcieElapsedMs(ReadyStart) >= PCIE_READY_TIMEOUT_MS)
            break;
        PcieSleepMs(1);
    }
    return (PcieRead(&Port->Registers, PORT_STATUS) & PORT_STATUS_READY) ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

NTSTATUS
NTAPI
ApplePcieOpen(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Node,
    _Out_ PAPPLE_PCIE Pcie)
{
    const UCHAR *Ranges, *BusRange;
    ULONG Child, Length, Index, Controller, Pin;
    BOOLEAN ActiveLow;
    CHAR PortName[] = "port0", PhyName[] = "phy0";
    PAPPLE_PCIE_PORT Port;

    NTSTATUS Status;

    RtlZeroMemory(Pcie, sizeof(*Pcie));
    Pcie->Node = Node;
    Pcie->PinctrlNode = FDT_NO_NODE;
    Status = ApplePmgrPowerOnDevice(Tree, Node);
    if (!NT_SUCCESS(Status))
        return Status;
    Index = FdtDriverFindRegIndex(Tree, Node, "config");
    if (Index == MAXULONG || !FdtDriverReadReg(Tree, Node, Index, &Pcie->ConfigBase, &Pcie->ConfigSize))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Pcie->LastBus = 255;
    BusRange = FdtGetProperty(&Tree->Fdt, Node, "bus-range", &Length);
    if (BusRange && Length == 2 * sizeof(ULONG))
    {
        Pcie->FirstBus = FdtReadBigEndian32(BusRange);
        Pcie->LastBus = FdtReadBigEndian32(BusRange + sizeof(ULONG));
    }
    if (Pcie->FirstBus > Pcie->LastBus || Pcie->LastBus > 255)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Ranges = FdtGetProperty(&Tree->Fdt, Node, "ranges", &Length);
    for (Index = 0; Ranges && (Index + 1) * 7 * sizeof(ULONG) <= Length && Pcie->WindowCount < APPLE_PCIE_MAX_WINDOWS;
         ++Index)
    {
        const UCHAR *Entry = Ranges + Index * 7 * sizeof(ULONG);
        ULONG Flags = FdtReadBigEndian32(Entry);
        ULONG64 Size = ((ULONG64)FdtReadBigEndian32(Entry + 20) << 32) | FdtReadBigEndian32(Entry + 24);

        if (((Flags >> 24) & 3) < 2 || !Size || Size > MAXULONG)
            continue;
        Pcie->Windows[Pcie->WindowCount].BusBase = ((ULONG64)FdtReadBigEndian32(Entry + 4) << 32) |
                                                   FdtReadBigEndian32(Entry + 8);
        Pcie->Windows[Pcie->WindowCount].Size = (ULONG)Size;
        Pcie->Windows[Pcie->WindowCount].Prefetchable = (Flags & 0x40000000) != 0;
        Pcie->WindowCount++;
    }
    Ranges = FdtGetProperty(&Tree->Fdt, Node, "msi-ranges", &Length);
    Pcie->MsiVectors = (Ranges && Length >= sizeof(ULONG)) ? FdtReadBigEndian32(Ranges + Length - sizeof(ULONG)) : 0;
    if (Pcie->MsiVectors > PCIE_MAX_MSI_VECTORS)
        Pcie->MsiVectors = PCIE_MAX_MSI_VECTORS;

    for (Child = FdtFirstChild(&Tree->Fdt, Node);
         Child != FDT_NO_NODE && Pcie->PortCount < APPLE_PCIE_MAX_PORTS;
         Child = FdtNextSibling(&Tree->Fdt, Child))
    {
        if (!PcieIsPciNode(Tree, Child) || !PcieNodeEnabled(Tree, Child))
            continue;
        Port = &Pcie->Ports[Pcie->PortCount];
        RtlZeroMemory(Port, sizeof(*Port));
        Port->Node = Child;
        BusRange = FdtGetProperty(&Tree->Fdt, Child, "bus-range", &Length);
        Port->Bus = (BusRange && Length >= sizeof(ULONG)) ? FdtReadBigEndian32(BusRange) : MAXULONG;
        Ranges = FdtGetProperty(&Tree->Fdt, Child, "reg", &Length);
        Port->Index = (Ranges && Length >= sizeof(ULONG)) ? (FdtReadBigEndian32(Ranges) >> 11) & 0x1F : MAXULONG;
        if (Port->Index >= APPLE_PCIE_MAX_PORTS)
            continue;
        PortName[4] = (CHAR)('0' + Port->Index);
        PhyName[3] = (CHAR)('0' + Port->Index);
        if (!PcieMapNamed(Tree, Node, PortName, PORT_SPAN, &Port->Registers) ||
            !PcieMapNamed(Tree, Node, PhyName, sizeof(ULONG), &Port->Phy))
        {
            PcieUnmap(&Port->Registers);
            continue;
        }
        if (Pcie->PinctrlNode == FDT_NO_NODE &&
            PcieReadGpio(Tree, Child, "reset-gpios", &Controller, &Pin, &ActiveLow))
            Pcie->PinctrlNode = Controller;
        Pcie->PortCount++;
    }
    if (!Pcie->PortCount)
        return STATUS_NO_SUCH_DEVICE;
    if (Pcie->PinctrlNode != FDT_NO_NODE && NT_SUCCESS(ApplePmgrPowerOnDevice(Tree, Pcie->PinctrlNode)))
    {
        Pcie->Pinctrl.Base = FdtDriverMapReg(Tree, Pcie->PinctrlNode, 0, NULL, &Pcie->Pinctrl.Size);
        PcieApplyPinmux(Tree, Pcie);
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
ApplePcieClose(
    _Inout_ PAPPLE_PCIE Pcie)
{
    ULONG Index;

    for (Index = 0; Index < Pcie->PortCount; ++Index)
    {
        PcieUnmap(&Pcie->Ports[Index].Registers);
        PcieUnmap(&Pcie->Ports[Index].Phy);
    }
    PcieUnmap(&Pcie->Pinctrl);
}

NTSTATUS
NTAPI
ApplePcieStartPort(
    _In_ PFDT_DRIVER_TREE Tree,
    _Inout_ PAPPLE_PCIE Pcie,
    _Inout_ PAPPLE_PCIE_PORT Port)
{
    ULONG Index;
    ULONG64 Start;
    NTSTATUS Status;

    Port->WasUp = (PcieRead(&Port->Registers, PORT_LINKSTS) & PORT_LINKSTS_UP) != 0;
    if (!Port->WasUp)
    {
        Status = PcieBringUpLink(Tree, Pcie, Port, FALSE);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    PcieMask(&Port->Phy, PHY_LANE_CFG, 0, PHY_LANE_CFG_REFCLKCGEN);
    PcieMask(&Port->Registers, PORT_APPCLK, PORT_APPCLK_CGDIS, 0);

    PcieWrite(&Port->Registers, PORT_INTMSK, MAXULONG);
    PcieWrite(&Port->Registers, PORT_INTSTAT, MAXULONG);
    PcieWrite(&Port->Registers, PORT_LINKCMDSTS, MAXULONG);
    PcieWrite(&Port->Registers, PORT_MSIADDR, APPLE_PCIE_MSI_DOORBELL);
    PcieWrite(&Port->Registers, PORT_MSIADDR_HI, 0);
    for (Index = 0; Index < Pcie->MsiVectors; ++Index)
        PcieWrite(&Port->Registers, PORT_MSIMAP + 4 * Index, Index | PORT_MSIMAP_ENABLE);
    PcieWrite(&Port->Registers, PORT_MSICFG, PORT_MSICFG_EN);

    for (Index = 0; Index < PORT_RID2SID_MAX && PORT_RID2SID + 4 * (Index + 1) <= Port->Registers.Size; ++Index)
    {
        PcieWrite(&Port->Registers, PORT_RID2SID + 4 * Index, PORT_RID2SID_PROBE);
        if (PcieRead(&Port->Registers, PORT_RID2SID + 4 * Index) != PORT_RID2SID_PROBE)
            break;
        PcieWrite(&Port->Registers, PORT_RID2SID + 4 * Index, 0);
    }
    Port->Rid2SidEntries = Index;
    Port->Rid2SidUsed = 0;

    if (!Port->WasUp)
    {
        Start = KeQueryInterruptTime();
        PcieWrite(&Port->Registers, PORT_LTSSMCTL, PORT_LTSSMCTL_START);
        while (!(PcieRead(&Port->Registers, PORT_LINKSTS) & PORT_LINKSTS_UP))
        {
            if (PcieElapsedMs(Start) >= PCIE_LINK_TIMEOUT_MS)
                break;
            PcieSleepMs(1);
        }
        Port->LinkWaitMs = PcieElapsedMs(Start);
        if (!(PcieRead(&Port->Registers, PORT_LINKSTS) & PORT_LINKSTS_UP))
        {
            PcieWrite(&Port->Registers, PORT_LTSSMCTL, 0);
            PcieMask(&Port->Registers, PORT_PERST, PORT_PERST_OFF, 0);
            Status = PcieBringUpLink(Tree, Pcie, Port, TRUE);
            if (NT_SUCCESS(Status))
            {
                Start = KeQueryInterruptTime();
                PcieWrite(&Port->Registers, PORT_LTSSMCTL, PORT_LTSSMCTL_START);
                while (!(PcieRead(&Port->Registers, PORT_LINKSTS) & PORT_LINKSTS_UP))
                {
                    if (PcieElapsedMs(Start) >= PCIE_LINK_TIMEOUT_MS)
                        break;
                    PcieSleepMs(1);
                }
                Port->LinkWaitMs = PcieElapsedMs(Start);
            }
        }
    }
    Port->LinkUp = (PcieRead(&Port->Registers, PORT_LINKSTS) & PORT_LINKSTS_UP) != 0;
    return Port->LinkUp ? STATUS_SUCCESS : STATUS_DEVICE_NOT_CONNECTED;
}

BOOLEAN
NTAPI
ApplePcieFindStream(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG PcieNode,
    _In_ ULONG Rid,
    _Out_ PULONG Phandle,
    _Out_ PULONG Sid)
{
    const UCHAR *Map;
    ULONG Length, Mask = 0xFFFF, Entry, Base, Span;

    *Phandle = 0;
    *Sid = 0;
    Map = FdtGetProperty(&Tree->Fdt, PcieNode, "iommu-map", &Length);
    if (!Map || Length % (4 * sizeof(ULONG)))
        return FALSE;
    FdtReadU32(&Tree->Fdt, PcieNode, "iommu-map-mask", &Mask);
    for (Entry = 0; Entry < Length / (4 * sizeof(ULONG)); ++Entry)
    {
        Base = FdtReadBigEndian32(Map + Entry * 16);
        Span = FdtReadBigEndian32(Map + Entry * 16 + 12);
        if ((Rid & Mask) >= Base && (Rid & Mask) - Base < Span)
        {
            *Phandle = FdtReadBigEndian32(Map + Entry * 16 + 4);
            *Sid = FdtReadBigEndian32(Map + Entry * 16 + 8) + (Rid & Mask) - Base;
            return TRUE;
        }
    }
    return FALSE;
}

ULONG
NTAPI
ApplePcieMapPortDevices(
    _In_ PFDT_DRIVER_TREE Tree,
    _Inout_ PAPPLE_PCIE Pcie,
    _Inout_ PAPPLE_PCIE_PORT Port)
{
    ULONG Function, Rid, Count = 0, Sid, Phandle;
    ULONG MappedDart[8], MappedSid[8], MappedCount = 0, Index;

    if (!Port->LinkUp || Port->Bus > 255)
        return 0;
    for (Function = 0; Function < 8; ++Function)
    {
        ULONG Id = ApplePcieReadConfig(Pcie, Port->Bus, 0, Function, 0);

        if (Id == MAXULONG || (Id & 0xFFFF) == 0xFFFF || Id == 0)
            continue;
        Rid = (Port->Bus << 8) | Function;
        if (!ApplePcieFindStream(Tree, Pcie->Node, Rid, &Phandle, &Sid) || Port->Rid2SidUsed >= Port->Rid2SidEntries)
            continue;
        PcieWrite(&Port->Registers, PORT_RID2SID + 4 * Port->Rid2SidUsed, PORT_RID2SID_VALID | (Sid << 16) | Rid);
        Port->Rid2SidUsed++;
        for (Index = 0; Index < MappedCount && (MappedDart[Index] != Phandle || MappedSid[Index] != Sid); ++Index)
            ;
        if (Index == MappedCount && MappedCount < RTL_NUMBER_OF(MappedDart) &&
            NT_SUCCESS(AppleDartMapStream(Tree, FdtDriverFindPhandle(Tree, Phandle), Sid, "applepcie")))
        {
            MappedDart[MappedCount] = Phandle;
            MappedSid[MappedCount] = Sid;
            MappedCount++;
        }
        Count++;
    }
    return Count;
}

ULONG
NTAPI
ApplePcieReadConfig(
    _In_ PAPPLE_PCIE Pcie,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ ULONG Offset)
{
    PHYSICAL_ADDRESS Physical;
    ULONG64 Address;
    PULONG Va;
    ULONG Value;

    Address = ((ULONG64)Bus << 20) | ((ULONG64)(Device & 0x1F) << 15) | ((ULONG64)(Function & 7) << 12) | (Offset & 0xFFC);
    if (Address + sizeof(ULONG) > Pcie->ConfigSize)
        return MAXULONG;
    Physical.QuadPart = (LONGLONG)(Pcie->ConfigBase + Address);
    Va = MmMapIoSpace(Physical, sizeof(ULONG), MmNonCached);
    if (!Va)
        return MAXULONG;
    Value = READ_REGISTER_ULONG((volatile ULONG *)Va);
    MmUnmapIoSpace(Va, sizeof(ULONG));
    return Value;
}

typedef struct _PCIE_CONFIG_SPACE
{
    volatile UCHAR *Base;
    ULONG Express;
    ULONG L1ss;
    ULONG Ltr;
} PCIE_CONFIG_SPACE, *PPCIE_CONFIG_SPACE;

static
ULONG
PcieConfigRead(
    _In_ PPCIE_CONFIG_SPACE Config,
    _In_ ULONG Offset)
{
    ASSERT(Config->Base && !(Offset & 3) && Offset < PCIE_CFG_PAGE);
    return READ_REGISTER_ULONG((volatile ULONG *)(Config->Base + Offset));
}

static
VOID
PcieConfigWrite(
    _In_ PPCIE_CONFIG_SPACE Config,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    ASSERT(Config->Base && !(Offset & 3) && Offset < PCIE_CFG_PAGE);
    WRITE_REGISTER_ULONG((volatile ULONG *)(Config->Base + Offset), Value);
}

static
VOID
PcieConfigControl(
    _In_ PPCIE_CONFIG_SPACE Config,
    _In_ ULONG Offset,
    _In_ ULONG Clear,
    _In_ ULONG Set)
{
    ASSERT(!((Clear | Set) & 0xFFFF0000UL));
    PcieConfigWrite(Config, Offset, ((PcieConfigRead(Config, Offset) & 0xFFFF) & ~Clear) | Set);
}

static
VOID
PcieUnmapConfig(
    _Inout_ PPCIE_CONFIG_SPACE Config)
{
    if (Config->Base)
        MmUnmapIoSpace((PVOID)Config->Base, PCIE_CFG_PAGE);
    RtlZeroMemory(Config, sizeof(*Config));
}

static
BOOLEAN
PcieMapConfig(
    _In_ PAPPLE_PCIE Pcie,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _Out_ PPCIE_CONFIG_SPACE Config)
{
    ULONG64 Offset = ((ULONG64)Bus << 20) | ((ULONG64)(Device & 0x1F) << 15) | ((ULONG64)(Function & 7) << 12);
    PHYSICAL_ADDRESS Physical;
    ULONG Pointer, Header, Walk;

    RtlZeroMemory(Config, sizeof(*Config));
    if (Offset + PCIE_CFG_PAGE > Pcie->ConfigSize)
        return FALSE;
    Physical.QuadPart = (LONGLONG)(Pcie->ConfigBase + Offset);
    Config->Base = MmMapIoSpace(Physical, PCIE_CFG_PAGE, MmNonCached);
    if (!Config->Base)
        return FALSE;
    Header = PcieConfigRead(Config, 0);
    if (Header == MAXULONG || Header == 0 || !(PcieConfigRead(Config, PCIE_CFG_STATUS) & PCIE_CFG_STATUS_CAPS))
    {
        PcieUnmapConfig(Config);
        return FALSE;
    }
    Pointer = PcieConfigRead(Config, PCIE_CFG_CAPABILITIES) & 0xFC;
    for (Walk = 0; Pointer >= PCIE_CFG_HEADER_END && Walk < PCIE_CFG_CAP_WALK_LIMIT; ++Walk)
    {
        Header = PcieConfigRead(Config, Pointer);
        if ((Header & 0xFF) == PCIE_CFG_CAP_EXPRESS)
        {
            Config->Express = Pointer;
            break;
        }
        Pointer = (Header >> 8) & 0xFC;
    }
    if (!Config->Express)
    {
        PcieUnmapConfig(Config);
        return FALSE;
    }
    for (Pointer = PCIE_CFG_EXTENDED, Walk = 0; Pointer >= PCIE_CFG_EXTENDED && Walk < PCIE_CFG_EXT_WALK_LIMIT; ++Walk)
    {
        Header = PcieConfigRead(Config, Pointer);
        if (Header == 0 || Header == MAXULONG)
            break;
        if ((Header & 0xFFFF) == PCIE_CFG_EXT_LTR)
            Config->Ltr = Pointer;
        else if ((Header & 0xFFFF) == PCIE_CFG_EXT_L1_PM_SS)
            Config->L1ss = Pointer;
        Pointer = (Header >> 20) & 0xFFC;
    }
    return TRUE;
}

static
BOOLEAN
PcieReadAdtWords(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Node,
    _In_z_ PCSTR Name,
    _Out_writes_(Count) PULONG Values,
    _In_ ULONG Count)
{
    const VOID *Property;
    ULONG Length;

    Property = AppleAdtGetProperty(Adt, Node, Name, &Length);
    if (!Property || Length != Count * sizeof(ULONG))
        return FALSE;
    RtlCopyMemory(Values, Property, Length);
    return TRUE;
}

static
BOOLEAN
PcieSupportsLtr(
    _In_ PPCIE_CONFIG_SPACE Config)
{
    return PCIE_EXP_VERSION(PcieConfigRead(Config, Config->Express)) >= 2 &&
           (PcieConfigRead(Config, Config->Express + PCIE_EXP_DEVICE_CAPS2) & PCIE_DEVCAP2_LTR) != 0;
}

ULONG
NTAPI
ApplePcieFindAdtController(
    _In_ PAPPLE_ADT Adt,
    _In_ PAPPLE_PCIE Pcie)
{
    ULONG Bus = AppleAdtPath(Adt, "/arm-io"), Node, Length, Seen, Count;
    const VOID *Type, *Reg;
    ULONG64 Base;

    if (Bus == APPLE_ADT_NO_NODE)
        return APPLE_ADT_NO_NODE;
    Count = AppleAdtChildCount(Adt, Bus);
    for (Node = AppleAdtFirstChild(Adt, Bus), Seen = 0; Node != APPLE_ADT_NO_NODE && Seen < Count;
         Node = AppleAdtNextSibling(Adt, Node), ++Seen)
    {
        Type = AppleAdtGetProperty(Adt, Node, "device_type", &Length);
        if (!Type || Length != sizeof("pci") || !RtlEqualMemory(Type, "pci", sizeof("pci")))
            continue;
        Reg = AppleAdtGetProperty(Adt, Node, "reg", &Length);
        if (!Reg || Length < sizeof(Base))
            continue;
        RtlCopyMemory(&Base, Reg, sizeof(Base));
        if (Base == Pcie->ConfigBase)
            return Node;
    }
    return APPLE_ADT_NO_NODE;
}

VOID
NTAPI
ApplePcieConfigureLinkPower(
    _In_ PAPPLE_ADT Adt,
    _In_ ULONG Controller,
    _In_ PAPPLE_PCIE Pcie,
    _In_ PAPPLE_PCIE_PORT Port)
{
    PCIE_CONFIG_SPACE Root, Functions[PCIE_CFG_MAX_FUNCTIONS];
    APPLE_PCIE_LINK_STATE State;
    APPLE_PCIE_LINK_POWER Power;
    ULONG RootL1pm[2], DeviceL1pm[2], Latency[1];
    ULONG Bridge, Child, Value, Index, Children = 0, Seen, Total;
    BOOLEAN HaveRootL1pm, HaveDeviceL1pm = FALSE, HaveLatency = FALSE;

    ASSERT(Port->LinkUp && Port->Bus != MAXULONG);

    Total = AppleAdtChildCount(Adt, Controller);
    for (Bridge = AppleAdtFirstChild(Adt, Controller), Seen = 0; Bridge != APPLE_ADT_NO_NODE && Seen < Total;
         Bridge = AppleAdtNextSibling(Adt, Bridge), ++Seen)
    {
        if (AppleAdtReadU32(Adt, Bridge, "apcie-port", &Value) && Value == Port->Index)
            break;
    }
    if (Bridge == APPLE_ADT_NO_NODE || Seen == Total)
    {
        DbgPrint("applepcie: port %lu has no bridge in the platform tree, link power management stays off\n", Port->Index);
        return;
    }

    RtlZeroMemory(&State, sizeof(State));
    State.PlatformAspm = APPLE_PCIE_ASPM_MASK;
    HaveRootL1pm = PcieReadAdtWords(Adt, Bridge, "pci-l1pm-control", RootL1pm, RTL_NUMBER_OF(RootL1pm));
    Total = AppleAdtChildCount(Adt, Bridge);
    for (Child = AppleAdtFirstChild(Adt, Bridge); Child != APPLE_ADT_NO_NODE && Children < Total;
         Child = AppleAdtNextSibling(Adt, Child))
    {
        ++Children;
        if (!AppleAdtReadU32(Adt, Child, "pci-aspm-default", &Value))
            Value = 0;
        State.PlatformAspm &= Value;
        if (!HaveDeviceL1pm && PcieReadAdtWords(Adt, Child, "pci-l1pm-control", DeviceL1pm, RTL_NUMBER_OF(DeviceL1pm)))
        {
            HaveDeviceL1pm = TRUE;
            HaveLatency = PcieReadAdtWords(Adt, Child, "pci-max-latency", Latency, RTL_NUMBER_OF(Latency));
        }
        else if (!HaveDeviceL1pm && !HaveLatency)
        {
            HaveLatency = PcieReadAdtWords(Adt, Child, "pci-max-latency", Latency, RTL_NUMBER_OF(Latency));
        }
    }
    if (!Children)
        State.PlatformAspm = 0;
    State.HavePlatformL1ss = HaveRootL1pm && HaveDeviceL1pm;
    State.RootL1ssControl1 = HaveRootL1pm ? RootL1pm[0] : 0;
    State.DeviceL1ssControl1 = HaveDeviceL1pm ? DeviceL1pm[0] : 0;
    State.HavePlatformLatency = HaveLatency;

    if (!PcieMapConfig(Pcie, 0, Port->Index, 0, &Root))
    {
        DbgPrint("applepcie: port %lu root port has no PCI Express capability, link power management stays off\n",
                 Port->Index);
        return;
    }
    RtlZeroMemory(Functions, sizeof(Functions));
    for (Index = 0; Index < PCIE_CFG_MAX_FUNCTIONS; ++Index)
    {
        if (!PcieMapConfig(Pcie, Port->Bus, 0, Index, &Functions[Index]))
        {
            if (Index == 0)
                break;
            continue;
        }
        State.DeviceLinkCapabilities[State.FunctionCount] =
            PcieConfigRead(&Functions[Index], Functions[Index].Express + PCIE_EXP_LINK_CAPS);
        State.DeviceCapabilities[State.FunctionCount] =
            PcieConfigRead(&Functions[Index], Functions[Index].Express + PCIE_EXP_DEVICE_CAPS);
        ++State.FunctionCount;
    }
    if (!Functions[0].Base)
    {
        DbgPrint("applepcie: port %lu device has no PCI Express capability, link power management stays off\n",
                 Port->Index);
        PcieUnmapConfig(&Root);
        return;
    }

    State.RootLinkCapabilities = PcieConfigRead(&Root, Root.Express + PCIE_EXP_LINK_CAPS);
    State.RootL1ssCapabilities = Root.L1ss ? PcieConfigRead(&Root, Root.L1ss + PCIE_L1SS_CAPABILITIES) : 0;
    State.RootSupportsLtr = PcieSupportsLtr(&Root);
    State.DeviceL1ssCapabilities =
        Functions[0].L1ss ? PcieConfigRead(&Functions[0], Functions[0].L1ss + PCIE_L1SS_CAPABILITIES) : 0;
    State.DeviceSupportsLtr = Functions[0].Ltr && PcieSupportsLtr(&Functions[0]);

    if (!ApplePcieComputeLinkPower(&State, &Power))
    {
        DbgPrint("applepcie: port %lu link power: the platform tree enables nothing both ends support, link left as found\n",
                 Port->Index);
        goto Done;
    }
    ASSERT(!Power.L1Substates || (Root.L1ss && Functions[0].L1ss && State.HavePlatformL1ss));
    ASSERT(!Power.Ltr || (Functions[0].Ltr && HaveLatency));

    for (Index = 0; Index < PCIE_CFG_MAX_FUNCTIONS; ++Index)
    {
        if (Functions[Index].Base)
            PcieConfigControl(&Functions[Index], Functions[Index].Express + PCIE_EXP_LINK_CONTROL, APPLE_PCIE_ASPM_MASK, 0);
    }
    PcieConfigControl(&Root, Root.Express + PCIE_EXP_LINK_CONTROL, APPLE_PCIE_ASPM_MASK, 0);
    if (Functions[0].L1ss)
        PcieConfigWrite(&Functions[0], Functions[0].L1ss + PCIE_L1SS_CONTROL1,
                        PcieConfigRead(&Functions[0], Functions[0].L1ss + PCIE_L1SS_CONTROL1) & ~APPLE_PCIE_L1SS_ENABLES);
    if (Root.L1ss)
        PcieConfigWrite(&Root, Root.L1ss + PCIE_L1SS_CONTROL1,
                        PcieConfigRead(&Root, Root.L1ss + PCIE_L1SS_CONTROL1) & ~APPLE_PCIE_L1SS_ENABLES);

    if (Power.Ltr)
    {
        PcieConfigWrite(&Functions[0], Functions[0].Ltr + PCIE_LTR_MAX_LATENCY, Latency[0]);
        PcieConfigControl(&Root, Root.Express + PCIE_EXP_DEVICE_CONTROL2, 0, PCIE_DEVCTL2_LTR);
        PcieConfigControl(&Functions[0], Functions[0].Express + PCIE_EXP_DEVICE_CONTROL2, 0, PCIE_DEVCTL2_LTR);
    }
    if (Power.L1Substates)
    {
        PcieConfigWrite(&Root, Root.L1ss + PCIE_L1SS_CONTROL2, RootL1pm[1]);
        PcieConfigWrite(&Functions[0], Functions[0].L1ss + PCIE_L1SS_CONTROL2, DeviceL1pm[1]);
        PcieConfigWrite(&Root, Root.L1ss + PCIE_L1SS_CONTROL1, RootL1pm[0] & ~APPLE_PCIE_L1SS_ENABLES);
        PcieConfigWrite(&Functions[0], Functions[0].L1ss + PCIE_L1SS_CONTROL1, DeviceL1pm[0] & ~APPLE_PCIE_L1SS_ENABLES);
        PcieConfigWrite(&Root, Root.L1ss + PCIE_L1SS_CONTROL1, (RootL1pm[0] & ~APPLE_PCIE_L1SS_ENABLES) | Power.L1Substates);
        PcieConfigWrite(&Functions[0], Functions[0].L1ss + PCIE_L1SS_CONTROL1,
                        (DeviceL1pm[0] & ~APPLE_PCIE_L1SS_ENABLES) | Power.L1Substates);
    }
    if (Power.Aspm)
    {
        PcieConfigControl(&Root, Root.Express + PCIE_EXP_LINK_CONTROL, 0, Power.Aspm);
        for (Index = 0; Index < PCIE_CFG_MAX_FUNCTIONS; ++Index)
        {
            if (Functions[Index].Base)
                PcieConfigControl(&Functions[Index], Functions[Index].Express + PCIE_EXP_LINK_CONTROL, 0, Power.Aspm);
        }
    }

    DbgPrint("applepcie: port %lu link power from the platform tree: ASPM %lu (both ends support %lu), L1 substates %lx, "
             "LTR %s %08lx, %lu functions; root link control %04lx L1SS %08lx/%08lx, device link control %04lx L1SS %08lx/%08lx\n",
             Port->Index, Power.Aspm, Power.Supported, Power.L1Substates, Power.Ltr ? "on" : "off",
             HaveLatency ? Latency[0] : 0, State.FunctionCount,
             PcieConfigRead(&Root, Root.Express + PCIE_EXP_LINK_CONTROL) & 0xFFFF,
             Root.L1ss ? PcieConfigRead(&Root, Root.L1ss + PCIE_L1SS_CONTROL1) : 0,
             Root.L1ss ? PcieConfigRead(&Root, Root.L1ss + PCIE_L1SS_CONTROL2) : 0,
             PcieConfigRead(&Functions[0], Functions[0].Express + PCIE_EXP_LINK_CONTROL) & 0xFFFF,
             Functions[0].L1ss ? PcieConfigRead(&Functions[0], Functions[0].L1ss + PCIE_L1SS_CONTROL1) : 0,
             Functions[0].L1ss ? PcieConfigRead(&Functions[0], Functions[0].L1ss + PCIE_L1SS_CONTROL2) : 0);

Done:
    for (Index = 0; Index < PCIE_CFG_MAX_FUNCTIONS; ++Index)
        PcieUnmapConfig(&Functions[Index]);
    PcieUnmapConfig(&Root);
}
