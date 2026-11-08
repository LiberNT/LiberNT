/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Describe a device-tree-only ARM64 platform to the OS with ACPI tables
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <arm64.h>
#include <blefi.h>
#include <Fdt.h>
#include <drivers/acpi/acpi.h>
#include <reactos/arm64/acpi.h>
/* TODO: move the FDT reader out of reactos/riscv64 now that ARM64 shares it */
#include <reactos/riscv64/fdtlib.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

#define FDTACPI_GTDT_SIGNATURE              0x54445447
#define FDTACPI_SPCR_SIGNATURE              0x52435053

#define FDTACPI_FADT_HW_REDUCED_ACPI        0x00100000
#define FDTACPI_FADT_PSCI_COMPLIANT         0x0001
#define FDTACPI_FADT_PSCI_USE_HVC           0x0002

#define FDTACPI_GICC_ENABLED                0x00000001
#define FDTACPI_MPIDR_AFFINITY_MASK         0x000000FF00FFFFFFULL

#define FDTACPI_GTDT_EDGE                   0x00000001
#define FDTACPI_GTDT_ACTIVE_LOW             0x00000002
#define FDTACPI_GTDT_ALWAYS_ON              0x00000004
#define FDTACPI_GTDT_NO_COUNTER_BLOCK       0xFFFFFFFFFFFFFFFFULL

#define FDTACPI_SERIAL_16550                0x00
#define FDTACPI_SERIAL_PL011                0x03
#define FDTACPI_SPCR_GIC_INTERRUPT          0x08
#define FDTACPI_SPCR_BAUD_115200            7
#define FDTACPI_GAS_ACCESS_BYTE             1
#define FDTACPI_GAS_ACCESS_DWORD            3

#define FDTACPI_GIC_SPI                     0
#define FDTACPI_GIC_PPI                     1
#define FDTACPI_GIC_SPI_BASE                32
#define FDTACPI_GIC_PPI_BASE                16
#define FDTACPI_IRQ_EDGE_RISING             0x1
#define FDTACPI_IRQ_EDGE_FALLING            0x2
#define FDTACPI_IRQ_LEVEL_LOW               0x8

#define FDTACPI_TIMER_SECURE                0
#define FDTACPI_TIMER_NON_SECURE            1
#define FDTACPI_TIMER_VIRTUAL               2
#define FDTACPI_TIMER_HYPERVISOR            3
#define FDTACPI_TIMER_COUNT                 4

#define FDTACPI_MAX_DEPTH                   16
#define FDTACPI_MAX_INTERRUPT_HOPS          8
#define FDTACPI_ALIGNMENT                   16

#define FDTACPI_AML_SCOPE_OP                0x10
#define FDTACPI_AML_NAME_OP                 0x08
#define FDTACPI_AML_STRING_PREFIX           0x0D
#define FDTACPI_AML_BYTE_PREFIX             0x0A
#define FDTACPI_AML_WORD_PREFIX             0x0B
#define FDTACPI_AML_ZERO_OP                 0x00
#define FDTACPI_AML_ONE_OP                  0x01
#define FDTACPI_AML_EXT_OP                  0x5B
#define FDTACPI_AML_DEVICE_OP               0x82
#define FDTACPI_AML_ROOT_CHAR               0x5C

C_ASSERT(sizeof(FADT) == 276);
C_ASSERT(sizeof(ARM64_ACPI_MADT_GENERIC_INTERRUPT) == 80);
C_ASSERT(sizeof(ARM64_ACPI_MADT_GENERIC_DISTRIBUTOR) == 24);
C_ASSERT(sizeof(ARM64_ACPI_MADT_GENERIC_REDISTRIBUTOR) == 16);
C_ASSERT(sizeof(ARM64_ACPI_GTDT) == 96);
C_ASSERT(sizeof(ARM64_ACPI_SPCR) == 80);
C_ASSERT(FIELD_OFFSET(FADT, arm_boot_arch) == 129);
C_ASSERT(FIELD_OFFSET(FADT, x_dsdt) == 140);

typedef struct _FDTACPI_PLATFORM
{
    RISCV_FDT Fdt;
    ULONG Root;
    ULONG Cpus;
    ULONG CpuCount;
    ULONG CpuAddressCells;
    ULONG Gic;
    ULONG GicVersion;
    ULONG GicInterruptCells;
    ULONGLONG GicdBase;
    ULONGLONG GiccBase;
    ULONGLONG GicrBase;
    ULONGLONG GicrLength;
    BOOLEAN PsciCompliant;
    BOOLEAN PsciUseHvc;
    BOOLEAN TimerPresent;
    ULONG TimerInterrupt[FDTACPI_TIMER_COUNT];
    ULONG TimerFlags[FDTACPI_TIMER_COUNT];
    BOOLEAN UartPresent;
    UCHAR UartInterfaceType;
    UCHAR UartBitWidth;
    UCHAR UartAccessSize;
    ULONGLONG UartBase;
    ULONG UartInterrupt;
} FDTACPI_PLATFORM, *PFDTACPI_PLATFORM;

typedef BOOLEAN
(*PFDTACPI_MATCH)(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_ const VOID *Context);

static const CHAR *const FdtAcpiGicV2Compatible[] =
{
    "arm,gic-400",
    "arm,cortex-a15-gic",
    "arm,cortex-a9-gic",
    "arm,cortex-a7-gic",
    NULL
};

static const CHAR *const FdtAcpiGicV3Compatible[] =
{
    "arm,gic-v3",
    NULL
};

static const CHAR *const FdtAcpiTimerCompatible[] =
{
    "arm,armv8-timer",
    "arm,armv7-timer",
    NULL
};

static const CHAR *const FdtAcpiPl011Compatible[] =
{
    "arm,pl011",
    NULL
};

static const CHAR *const FdtAcpiNs16550Compatible[] =
{
    "ns16550a",
    "ns16550",
    "nvidia,tegra20-uart",
    "snps,dw-apb-uart",
    NULL
};

static
BOOLEAN
FdtAcpiIsCompatible(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_ const CHAR *const *Names)
{
    const VOID *Compatible;
    ULONG Length;
    ULONG Index;

    Compatible = RiscvFdtGetProperty(Fdt, Node, "compatible", &Length);
    if (!Compatible)
        return FALSE;

    for (Index = 0; Names[Index]; Index++)
    {
        if (RiscvFdtStringListContains(Compatible, Length, Names[Index]))
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
FdtAcpiIsEnabled(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node)
{
    const CHAR *Status;
    ULONG Length;

    Status = RiscvFdtGetProperty(Fdt, Node, "status", &Length);
    if (!Status || Length == 0)
        return TRUE;

    return (strncmp(Status, "okay", Length) == 0) || (strncmp(Status, "ok", Length) == 0);
}

static
BOOLEAN
FdtAcpiMatchCompatible(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_ const VOID *Context)
{
    return FdtAcpiIsEnabled(Fdt, Node) &&
           FdtAcpiIsCompatible(Fdt, Node, (const CHAR *const *)Context);
}

static
BOOLEAN
FdtAcpiMatchPhandle(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_ const VOID *Context)
{
    ULONG Phandle;

    if (!RiscvFdtReadU32(Fdt, Node, "phandle", &Phandle) &&
        !RiscvFdtReadU32(Fdt, Node, "linux,phandle", &Phandle))
    {
        return FALSE;
    }

    return Phandle == *(const ULONG *)Context;
}

static
BOOLEAN
FdtAcpiFindNodeBelow(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _In_ ULONG Depth,
    _In_ PFDTACPI_MATCH Match,
    _In_ const VOID *Context,
    _Out_ PULONG Found,
    _Out_ PULONG FoundParent)
{
    ULONG Child;

    if (Match(Fdt, Node, Context))
    {
        *Found = Node;
        *FoundParent = Parent;
        return TRUE;
    }

    if (Depth >= FDTACPI_MAX_DEPTH)
        return FALSE;

    for (Child = RiscvFdtFirstChild(Fdt, Node);
         Child != RISCV_FDT_NO_NODE;
         Child = RiscvFdtNextSibling(Fdt, Child))
    {
        if (FdtAcpiFindNodeBelow(Fdt, Child, Node, Depth + 1, Match, Context, Found, FoundParent))
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
FdtAcpiFindNode(
    _In_ PFDTACPI_PLATFORM Platform,
    _In_ PFDTACPI_MATCH Match,
    _In_ const VOID *Context,
    _Out_ PULONG Found,
    _Out_ PULONG FoundParent)
{
    return FdtAcpiFindNodeBelow(&Platform->Fdt,
                                Platform->Root,
                                RISCV_FDT_NO_NODE,
                                0,
                                Match,
                                Context,
                                Found,
                                FoundParent);
}

/* Only nodes whose bus addresses are CPU physical addresses can be described. */
static
BOOLEAN
FdtAcpiParentMapsOneToOne(
    _In_ PFDTACPI_PLATFORM Platform,
    _In_ ULONG Parent)
{
    ULONG Length;

    if (Parent == Platform->Root)
        return TRUE;

    /* TODO: translate addresses through a non-empty parent "ranges" */
    return RiscvFdtGetProperty(&Platform->Fdt, Parent, "ranges", &Length) != NULL && Length == 0;
}

static
BOOLEAN
FdtAcpiReadInterruptParent(
    _In_ PFDTACPI_PLATFORM Platform,
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _Out_ PULONG Phandle)
{
    if (RiscvFdtReadU32(&Platform->Fdt, Node, "interrupt-parent", Phandle))
        return TRUE;

    /* TODO: inherit interrupt-parent from ancestors above the direct parent */
    if (Parent != RISCV_FDT_NO_NODE &&
        RiscvFdtReadU32(&Platform->Fdt, Parent, "interrupt-parent", Phandle))
    {
        return TRUE;
    }

    return RiscvFdtReadU32(&Platform->Fdt, Platform->Root, "interrupt-parent", Phandle);
}

static
BOOLEAN
FdtAcpiNodeFromPhandle(
    _In_ PFDTACPI_PLATFORM Platform,
    _In_ ULONG Phandle,
    _Out_ PULONG Node,
    _Out_ PULONG Parent)
{
    return FdtAcpiFindNode(Platform, FdtAcpiMatchPhandle, &Phandle, Node, Parent);
}

static
BOOLEAN
FdtAcpiIsGic(
    _In_ PFDTACPI_PLATFORM Platform,
    _In_ ULONG Node,
    _Out_ PULONG Version)
{
    if (FdtAcpiIsCompatible(&Platform->Fdt, Node, FdtAcpiGicV3Compatible))
    {
        *Version = 3;
        return TRUE;
    }

    if (FdtAcpiIsCompatible(&Platform->Fdt, Node, FdtAcpiGicV2Compatible))
    {
        *Version = 2;
        return TRUE;
    }

    return FALSE;
}

/* Follow the interrupt-parent chain from the root to the GIC that ends it. */
static
BOOLEAN
FdtAcpiLocateGic(
    _In_ PFDTACPI_PLATFORM Platform,
    _Out_ PULONG GicParent)
{
    ULONG Node = Platform->Root;
    ULONG Parent = RISCV_FDT_NO_NODE;
    ULONG Phandle;
    ULONG Hop;

    for (Hop = 0; Hop < FDTACPI_MAX_INTERRUPT_HOPS; Hop++)
    {
        if (!RiscvFdtReadU32(&Platform->Fdt, Node, "interrupt-parent", &Phandle) ||
            !FdtAcpiNodeFromPhandle(Platform, Phandle, &Node, &Parent))
        {
            break;
        }

        if (FdtAcpiIsGic(Platform, Node, &Platform->GicVersion))
        {
            Platform->Gic = Node;
            *GicParent = Parent;
            return TRUE;
        }
    }

    if (FdtAcpiFindNode(Platform, FdtAcpiMatchCompatible, FdtAcpiGicV3Compatible, &Node, &Parent) ||
        FdtAcpiFindNode(Platform, FdtAcpiMatchCompatible, FdtAcpiGicV2Compatible, &Node, &Parent))
    {
        Platform->Gic = Node;
        *GicParent = Parent;
        return FdtAcpiIsGic(Platform, Node, &Platform->GicVersion);
    }

    return FALSE;
}

static
BOOLEAN
FdtAcpiDescribeGic(
    _Inout_ PFDTACPI_PLATFORM Platform)
{
    ULONGLONG Size;
    ULONG Parent;

    if (!FdtAcpiLocateGic(Platform, &Parent) ||
        !FdtAcpiParentMapsOneToOne(Platform, Parent) ||
        !RiscvFdtReadReg(&Platform->Fdt, Platform->Gic, Parent, 0, &Platform->GicdBase, &Size))
    {
        ERR("ARM64: the device tree has no usable GIC\n");
        return FALSE;
    }

    if (!RiscvFdtReadU32(&Platform->Fdt, Platform->Gic, "#interrupt-cells", &Platform->GicInterruptCells))
        Platform->GicInterruptCells = 0;

    if (Platform->GicVersion == 3)
    {
        if (!RiscvFdtReadReg(&Platform->Fdt, Platform->Gic, Parent, 1, &Platform->GicrBase, &Platform->GicrLength))
            return FALSE;

        /* TODO: describe more than the first "#redistributor-regions" entry */
        return TRUE;
    }

    return RiscvFdtReadReg(&Platform->Fdt, Platform->Gic, Parent, 1, &Platform->GiccBase, &Size);
}

static
BOOLEAN
FdtAcpiIsCpu(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node)
{
    const CHAR *DeviceType;
    ULONG Length;

    DeviceType = RiscvFdtGetProperty(Fdt, Node, "device_type", &Length);
    return DeviceType && Length == sizeof("cpu") && !memcmp(DeviceType, "cpu", sizeof("cpu")) &&
           FdtAcpiIsEnabled(Fdt, Node);
}

static
BOOLEAN
FdtAcpiReadCpuMpidr(
    _In_ PFDTACPI_PLATFORM Platform,
    _In_ ULONG Node,
    _Out_ PULONGLONG Mpidr)
{
    const VOID *Reg;
    ULONG Length;

    Reg = RiscvFdtGetProperty(&Platform->Fdt, Node, "reg", &Length);
    if (!Reg || !RiscvFdtReadCells(Reg, Length, 0, Platform->CpuAddressCells, Mpidr))
        return FALSE;

    *Mpidr &= FDTACPI_MPIDR_AFFINITY_MASK;
    return TRUE;
}

static
BOOLEAN
FdtAcpiDescribeCpus(
    _Inout_ PFDTACPI_PLATFORM Platform)
{
    ULONG SizeCells;
    ULONG Node;
    ULONGLONG Mpidr;

    if (!RiscvFdtFindNode(&Platform->Fdt, "/cpus", &Platform->Cpus, NULL))
    {
        ERR("ARM64: the device tree has no /cpus node\n");
        return FALSE;
    }

    RiscvFdtGetCellCounts(&Platform->Fdt, Platform->Cpus, &Platform->CpuAddressCells, &SizeCells);

    for (Node = RiscvFdtFirstChild(&Platform->Fdt, Platform->Cpus);
         Node != RISCV_FDT_NO_NODE;
         Node = RiscvFdtNextSibling(&Platform->Fdt, Node))
    {
        if (FdtAcpiIsCpu(&Platform->Fdt, Node) && FdtAcpiReadCpuMpidr(Platform, Node, &Mpidr))
            Platform->CpuCount++;
    }

    if (Platform->CpuCount == 0)
    {
        ERR("ARM64: the device tree lists no enabled CPU\n");
        return FALSE;
    }

    return TRUE;
}

static
VOID
FdtAcpiDescribePsci(
    _Inout_ PFDTACPI_PLATFORM Platform)
{
    static const CHAR *const Compatible[] = { "arm,psci-1.0", "arm,psci-0.2", NULL };
    const CHAR *Method;
    ULONG Length;
    ULONG Node;

    if (!RiscvFdtFindNode(&Platform->Fdt, "/psci", &Node, NULL) ||
        !FdtAcpiIsCompatible(&Platform->Fdt, Node, Compatible))
    {
        return;
    }

    Method = RiscvFdtGetProperty(&Platform->Fdt, Node, "method", &Length);
    if (!Method || Length != sizeof("smc"))
        return;

    if (!memcmp(Method, "hvc", sizeof("hvc")))
        Platform->PsciUseHvc = TRUE;
    else if (memcmp(Method, "smc", sizeof("smc")))
        return;

    Platform->PsciCompliant = TRUE;
}

/* Turn one GIC interrupt specifier into a GSIV and its trigger flags. */
static
BOOLEAN
FdtAcpiDecodeGicInterrupt(
    _In_ PFDTACPI_PLATFORM Platform,
    _In_reads_bytes_(Length) const VOID *Interrupts,
    _In_ ULONG Length,
    _In_ ULONG Index,
    _Out_ PULONG Gsiv,
    _Out_ PULONG Trigger)
{
    ULONGLONG Type;
    ULONGLONG Number;
    ULONGLONG Flags;
    ULONG First = Index * Platform->GicInterruptCells;

    if (Platform->GicInterruptCells != 3 ||
        !RiscvFdtReadCells(Interrupts, Length, First, 1, &Type) ||
        !RiscvFdtReadCells(Interrupts, Length, First + 1, 1, &Number) ||
        !RiscvFdtReadCells(Interrupts, Length, First + 2, 1, &Flags))
    {
        return FALSE;
    }

    if (Type == FDTACPI_GIC_PPI)
        *Gsiv = (ULONG)Number + FDTACPI_GIC_PPI_BASE;
    else if (Type == FDTACPI_GIC_SPI)
        *Gsiv = (ULONG)Number + FDTACPI_GIC_SPI_BASE;
    else
        return FALSE;

    *Trigger = (ULONG)Flags & 0xF;
    return TRUE;
}

static
BOOLEAN
FdtAcpiInterruptsGoToGic(
    _In_ PFDTACPI_PLATFORM Platform,
    _In_ ULONG Node,
    _In_ ULONG Parent)
{
    ULONG Phandle;
    ULONG Controller;
    ULONG ControllerParent;

    return FdtAcpiReadInterruptParent(Platform, Node, Parent, &Phandle) &&
           FdtAcpiNodeFromPhandle(Platform, Phandle, &Controller, &ControllerParent) &&
           Controller == Platform->Gic;
}

static
VOID
FdtAcpiDescribeTimer(
    _Inout_ PFDTACPI_PLATFORM Platform)
{
    const VOID *Interrupts;
    ULONG Length;
    ULONG Node;
    ULONG Parent;
    ULONG Index;
    ULONG Trigger;
    ULONG AlwaysOnLength;
    BOOLEAN AlwaysOn;

    if (!FdtAcpiFindNode(Platform, FdtAcpiMatchCompatible, FdtAcpiTimerCompatible, &Node, &Parent) ||
        !FdtAcpiInterruptsGoToGic(Platform, Node, Parent))
    {
        return;
    }

    Interrupts = RiscvFdtGetProperty(&Platform->Fdt, Node, "interrupts", &Length);
    if (!Interrupts)
        return;

    AlwaysOn = RiscvFdtGetProperty(&Platform->Fdt, Node, "always-on", &AlwaysOnLength) != NULL;

    for (Index = 0; Index < FDTACPI_TIMER_COUNT; Index++)
    {
        if (!FdtAcpiDecodeGicInterrupt(Platform,
                                       Interrupts,
                                       Length,
                                       Index,
                                       &Platform->TimerInterrupt[Index],
                                       &Trigger))
        {
            return;
        }

        Platform->TimerFlags[Index] = AlwaysOn ? FDTACPI_GTDT_ALWAYS_ON : 0;
        if (Trigger == FDTACPI_IRQ_EDGE_RISING || Trigger == FDTACPI_IRQ_EDGE_FALLING)
            Platform->TimerFlags[Index] |= FDTACPI_GTDT_EDGE;
        if (Trigger == FDTACPI_IRQ_EDGE_FALLING || Trigger == FDTACPI_IRQ_LEVEL_LOW)
            Platform->TimerFlags[Index] |= FDTACPI_GTDT_ACTIVE_LOW;
    }

    Platform->TimerPresent = TRUE;
}

static
VOID
FdtAcpiDescribeUart(
    _Inout_ PFDTACPI_PLATFORM Platform)
{
    const CHAR *Path;
    const VOID *Interrupts;
    ULONG Length;
    ULONG Chosen;
    ULONG Node;
    ULONG Parent;
    ULONG RegShift = 0;
    ULONG IoWidth = 1;
    ULONG Trigger;
    ULONGLONG Size;

    if (!RiscvFdtFindNode(&Platform->Fdt, "/chosen", &Chosen, NULL))
        return;

    Path = RiscvFdtGetProperty(&Platform->Fdt, Chosen, "stdout-path", &Length);
    if (!Path || Length == 0 || Path[Length - 1] != '\0' ||
        !RiscvFdtResolvePath(&Platform->Fdt, Path, &Node, &Parent, NULL) ||
        Parent == RISCV_FDT_NO_NODE || !FdtAcpiIsEnabled(&Platform->Fdt, Node) ||
        !FdtAcpiParentMapsOneToOne(Platform, Parent) ||
        !RiscvFdtReadReg(&Platform->Fdt, Node, Parent, 0, &Platform->UartBase, &Size))
    {
        return;
    }

    if (FdtAcpiIsCompatible(&Platform->Fdt, Node, FdtAcpiPl011Compatible))
    {
        Platform->UartInterfaceType = FDTACPI_SERIAL_PL011;
        Platform->UartBitWidth = 32;
        Platform->UartAccessSize = FDTACPI_GAS_ACCESS_DWORD;
    }
    else if (FdtAcpiIsCompatible(&Platform->Fdt, Node, FdtAcpiNs16550Compatible))
    {
        RiscvFdtReadU32(&Platform->Fdt, Node, "reg-shift", &RegShift);
        RiscvFdtReadU32(&Platform->Fdt, Node, "reg-io-width", &IoWidth);

        /* TODO: describe 16550 UARTs with spaced registers once the early UART honors the stride */
        if (RegShift != 0 || IoWidth != 1)
            return;

        Platform->UartInterfaceType = FDTACPI_SERIAL_16550;
        Platform->UartBitWidth = 8;
        Platform->UartAccessSize = FDTACPI_GAS_ACCESS_BYTE;
    }
    else
    {
        return;
    }

    Interrupts = RiscvFdtGetProperty(&Platform->Fdt, Node, "interrupts", &Length);
    if (Interrupts && FdtAcpiInterruptsGoToGic(Platform, Node, Parent) &&
        !FdtAcpiDecodeGicInterrupt(Platform, Interrupts, Length, 0, &Platform->UartInterrupt, &Trigger))
    {
        Platform->UartInterrupt = 0;
    }

    Platform->UartPresent = TRUE;
}

static
VOID
FdtAcpiFillHeader(
    _Out_ PDESCRIPTION_HEADER Header,
    _In_ ULONG Signature,
    _In_ ULONG Length,
    _In_ UCHAR Revision)
{
    Header->Signature = Signature;
    Header->Length = Length;
    Header->Revision = Revision;
    RtlCopyMemory(Header->OEMID, "LBRNT ", sizeof(Header->OEMID));
    RtlCopyMemory(Header->OEMTableID, "DTPLATFM", sizeof(Header->OEMTableID));
    Header->OEMRevision = 1;
    RtlCopyMemory(Header->CreatorID, "LBRN", sizeof(Header->CreatorID));
    Header->CreatorRev = 1;
}

static
UCHAR
FdtAcpiChecksum(
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length)
{
    const UCHAR *Bytes = Buffer;
    UCHAR Sum = 0;
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
        Sum = (UCHAR)(Sum + Bytes[Index]);

    return (UCHAR)(0 - Sum);
}

static
VOID
FdtAcpiSealTable(
    _Inout_ PDESCRIPTION_HEADER Header)
{
    Header->Checksum = 0;
    Header->Checksum = FdtAcpiChecksum(Header, Header->Length);
}

static
ULONG
FdtAcpiAmlPackageLengthSize(
    _In_ ULONG Body)
{
    if (Body + 1 <= 0x3F)
        return 1;
    if (Body + 2 <= 0xFFF)
        return 2;
    return 3;
}

static
ULONG
FdtAcpiAmlWritePackageLength(
    _Out_writes_bytes_opt_(3) PUCHAR Out,
    _In_ ULONG Body)
{
    ULONG Size = FdtAcpiAmlPackageLengthSize(Body);
    ULONG Total = Body + Size;

    if (!Out)
        return Size;

    if (Size == 1)
    {
        Out[0] = (UCHAR)Total;
    }
    else if (Size == 2)
    {
        Out[0] = (UCHAR)(0x40 | (Total & 0xF));
        Out[1] = (UCHAR)(Total >> 4);
    }
    else
    {
        Out[0] = (UCHAR)(0x80 | (Total & 0xF));
        Out[1] = (UCHAR)(Total >> 4);
        Out[2] = (UCHAR)(Total >> 12);
    }

    return Size;
}

static
ULONG
FdtAcpiAmlIntegerSize(
    _In_ ULONG Value)
{
    if (Value <= 1)
        return 1;
    return (Value <= 0xFF) ? 2 : 3;
}

static
ULONG
FdtAcpiAmlWriteInteger(
    _Out_writes_bytes_opt_(3) PUCHAR Out,
    _In_ ULONG Value)
{
    ULONG Size = FdtAcpiAmlIntegerSize(Value);

    if (!Out)
        return Size;

    if (Value == 0)
    {
        Out[0] = FDTACPI_AML_ZERO_OP;
    }
    else if (Value == 1)
    {
        Out[0] = FDTACPI_AML_ONE_OP;
    }
    else if (Size == 2)
    {
        Out[0] = FDTACPI_AML_BYTE_PREFIX;
        Out[1] = (UCHAR)Value;
    }
    else
    {
        Out[0] = FDTACPI_AML_WORD_PREFIX;
        Out[1] = (UCHAR)Value;
        Out[2] = (UCHAR)(Value >> 8);
    }

    return Size;
}

/* Device (Cxxx) { Name (_HID, "ACPI0007") Name (_UID, Uid) } */
static
ULONG
FdtAcpiAmlWriteProcessor(
    _Out_writes_bytes_opt_(32) PUCHAR Out,
    _In_ ULONG Uid)
{
    static const CHAR Hex[] = "0123456789ABCDEF";
    static const UCHAR Hid[] = { FDTACPI_AML_NAME_OP, '_', 'H', 'I', 'D', FDTACPI_AML_STRING_PREFIX,
                                 'A', 'C', 'P', 'I', '0', '0', '0', '7', 0 };
    static const UCHAR UidName[] = { FDTACPI_AML_NAME_OP, '_', 'U', 'I', 'D' };
    ULONG Body = 4 + sizeof(Hid) + sizeof(UidName) + FdtAcpiAmlIntegerSize(Uid);
    ULONG Offset;

    if (!Out)
        return 2 + FdtAcpiAmlPackageLengthSize(Body) + Body;

    Out[0] = FDTACPI_AML_EXT_OP;
    Out[1] = FDTACPI_AML_DEVICE_OP;
    Offset = 2 + FdtAcpiAmlWritePackageLength(Out + 2, Body);
    Out[Offset++] = 'C';
    Out[Offset++] = Hex[(Uid >> 8) & 0xF];
    Out[Offset++] = Hex[(Uid >> 4) & 0xF];
    Out[Offset++] = Hex[Uid & 0xF];
    RtlCopyMemory(Out + Offset, Hid, sizeof(Hid));
    Offset += sizeof(Hid);
    RtlCopyMemory(Out + Offset, UidName, sizeof(UidName));
    Offset += sizeof(UidName);
    Offset += FdtAcpiAmlWriteInteger(Out + Offset, Uid);
    return Offset;
}

/* Scope (\_SB) { one processor device per enabled CPU } */
static
ULONG
FdtAcpiAmlWriteDefinitionBlock(
    _Out_opt_ PUCHAR Out,
    _In_ ULONG CpuCount)
{
    static const UCHAR Sb[] = { FDTACPI_AML_ROOT_CHAR, '_', 'S', 'B', '_' };
    ULONG Body = sizeof(Sb);
    ULONG Offset;
    ULONG Uid;

    for (Uid = 0; Uid < CpuCount; Uid++)
        Body += FdtAcpiAmlWriteProcessor(NULL, Uid);

    if (!Out)
        return 1 + FdtAcpiAmlPackageLengthSize(Body) + Body;

    Out[0] = FDTACPI_AML_SCOPE_OP;
    Offset = 1 + FdtAcpiAmlWritePackageLength(Out + 1, Body);
    RtlCopyMemory(Out + Offset, Sb, sizeof(Sb));
    Offset += sizeof(Sb);

    for (Uid = 0; Uid < CpuCount; Uid++)
        Offset += FdtAcpiAmlWriteProcessor(Out + Offset, Uid);

    return Offset;
}

static
ULONG
FdtAcpiMadtLength(
    _In_ PFDTACPI_PLATFORM Platform)
{
    ULONG Length = sizeof(ARM64_ACPI_MADT) +
                   Platform->CpuCount * sizeof(ARM64_ACPI_MADT_GENERIC_INTERRUPT) +
                   sizeof(ARM64_ACPI_MADT_GENERIC_DISTRIBUTOR);

    if (Platform->GicVersion == 3)
        Length += sizeof(ARM64_ACPI_MADT_GENERIC_REDISTRIBUTOR);

    return Length;
}

static
VOID
FdtAcpiBuildMadt(
    _In_ PFDTACPI_PLATFORM Platform,
    _Out_ PARM64_ACPI_MADT Madt)
{
    PARM64_ACPI_MADT_GENERIC_INTERRUPT Gicc;
    PARM64_ACPI_MADT_GENERIC_DISTRIBUTOR Gicd;
    PARM64_ACPI_MADT_GENERIC_REDISTRIBUTOR Gicr;
    ULONG Length = FdtAcpiMadtLength(Platform);
    ULONG Node;
    ULONG Index = 0;
    ULONGLONG Mpidr;

    FdtAcpiFillHeader(&Madt->Header, APIC_SIGNATURE, Length, 5);
    Gicc = (PARM64_ACPI_MADT_GENERIC_INTERRUPT)(Madt + 1);

    for (Node = RiscvFdtFirstChild(&Platform->Fdt, Platform->Cpus);
         Node != RISCV_FDT_NO_NODE && Index < Platform->CpuCount;
         Node = RiscvFdtNextSibling(&Platform->Fdt, Node))
    {
        if (!FdtAcpiIsCpu(&Platform->Fdt, Node) || !FdtAcpiReadCpuMpidr(Platform, Node, &Mpidr))
            continue;

        Gicc->Header.Type = ARM64_ACPI_MADT_TYPE_GENERIC_INTERRUPT;
        Gicc->Header.Length = sizeof(*Gicc);
        /* FIXME: GICv2 CPU interface numbers assume the device-tree CPU order */
        Gicc->CpuInterfaceNumber = Index;
        Gicc->AcpiProcessorUid = Index;
        Gicc->Flags = FDTACPI_GICC_ENABLED;
        Gicc->BaseAddress = Platform->GiccBase;
        Gicc->Mpidr = Mpidr;
        Gicc++;
        Index++;
    }

    Gicd = (PARM64_ACPI_MADT_GENERIC_DISTRIBUTOR)Gicc;
    Gicd->Header.Type = ARM64_ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR;
    Gicd->Header.Length = sizeof(*Gicd);
    Gicd->BaseAddress = Platform->GicdBase;
    Gicd->GicVersion = (UCHAR)Platform->GicVersion;

    if (Platform->GicVersion == 3)
    {
        Gicr = (PARM64_ACPI_MADT_GENERIC_REDISTRIBUTOR)(Gicd + 1);
        Gicr->Header.Type = ARM64_ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR;
        Gicr->Header.Length = sizeof(*Gicr);
        Gicr->BaseAddress = Platform->GicrBase;
        Gicr->Length = (ULONG)Platform->GicrLength;
    }
}

static
VOID
FdtAcpiBuildGtdt(
    _In_ PFDTACPI_PLATFORM Platform,
    _Out_ PARM64_ACPI_GTDT Gtdt)
{
    FdtAcpiFillHeader(&Gtdt->Header, FDTACPI_GTDT_SIGNATURE, sizeof(*Gtdt), 2);
    Gtdt->CounterBlockAddress = FDTACPI_GTDT_NO_COUNTER_BLOCK;
    Gtdt->CounterReadBlockAddress = FDTACPI_GTDT_NO_COUNTER_BLOCK;
    Gtdt->SecureEl1Interrupt = Platform->TimerInterrupt[FDTACPI_TIMER_SECURE];
    Gtdt->SecureEl1Flags = Platform->TimerFlags[FDTACPI_TIMER_SECURE];
    Gtdt->NonSecureEl1Interrupt = Platform->TimerInterrupt[FDTACPI_TIMER_NON_SECURE];
    Gtdt->NonSecureEl1Flags = Platform->TimerFlags[FDTACPI_TIMER_NON_SECURE];
    Gtdt->VirtualTimerInterrupt = Platform->TimerInterrupt[FDTACPI_TIMER_VIRTUAL];
    Gtdt->VirtualTimerFlags = Platform->TimerFlags[FDTACPI_TIMER_VIRTUAL];
    Gtdt->NonSecureEl2Interrupt = Platform->TimerInterrupt[FDTACPI_TIMER_HYPERVISOR];
    Gtdt->NonSecureEl2Flags = Platform->TimerFlags[FDTACPI_TIMER_HYPERVISOR];
}

static
VOID
FdtAcpiBuildSpcr(
    _In_ PFDTACPI_PLATFORM Platform,
    _Out_ PARM64_ACPI_SPCR Spcr)
{
    FdtAcpiFillHeader(&Spcr->Header, FDTACPI_SPCR_SIGNATURE, sizeof(*Spcr), 2);
    Spcr->InterfaceType = Platform->UartInterfaceType;
    Spcr->SerialPort.AddressSpaceID = 0;
    Spcr->SerialPort.BitWidth = Platform->UartBitWidth;
    Spcr->SerialPort.AccessSize = Platform->UartAccessSize;
    Spcr->SerialPort.Address.QuadPart = (LONGLONG)Platform->UartBase;
    if (Platform->UartInterrupt)
    {
        Spcr->InterruptType = FDTACPI_SPCR_GIC_INTERRUPT;
        Spcr->Interrupt = Platform->UartInterrupt;
    }
    Spcr->BaudRate = FDTACPI_SPCR_BAUD_115200;
    Spcr->StopBits = 1;
    Spcr->PciDeviceId = 0xFFFF;
    Spcr->PciVendorId = 0xFFFF;
}

static
VOID
FdtAcpiBuildFadt(
    _In_ PFDTACPI_PLATFORM Platform,
    _Out_ PFADT Fadt,
    _In_ ULONGLONG Dsdt)
{
    FdtAcpiFillHeader(&Fadt->Header, FADT_SIGNATURE, sizeof(*Fadt), 6);
    Fadt->minor_revision = 3;
    Fadt->flags = FDTACPI_FADT_HW_REDUCED_ACPI;
    Fadt->dsdt = (ULONG)Dsdt;
    Fadt->x_dsdt.QuadPart = (LONGLONG)Dsdt;
    if (Platform->PsciCompliant)
    {
        Fadt->arm_boot_arch = FDTACPI_FADT_PSCI_COMPLIANT;
        if (Platform->PsciUseHvc)
            Fadt->arm_boot_arch |= FDTACPI_FADT_PSCI_USE_HVC;
    }
}

static
ULONG
FdtAcpiAlign(
    _In_ ULONG Value)
{
    return (Value + FDTACPI_ALIGNMENT - 1) & ~(FDTACPI_ALIGNMENT - 1);
}

static
BOOLEAN
FdtAcpiInstallTables(
    _In_ PFDTACPI_PLATFORM Platform)
{
    EFI_GUID Acpi20Guid = EFI_ACPI_20_TABLE_GUID;
    EFI_PHYSICAL_ADDRESS Memory = 0xFFFFFFFF;
    EFI_STATUS Status;
    ULONG TableCount = 2 + (Platform->TimerPresent ? 1 : 0) + (Platform->UartPresent ? 1 : 0);
    ULONG RsdpOffset = 0;
    ULONG XsdtOffset = FdtAcpiAlign(sizeof(RSDP));
    ULONG XsdtLength = FIELD_OFFSET(XSDT, Tables) + TableCount * sizeof(PHYSICAL_ADDRESS);
    ULONG FadtOffset = FdtAcpiAlign(XsdtOffset + XsdtLength);
    ULONG MadtOffset = FdtAcpiAlign(FadtOffset + sizeof(FADT));
    ULONG GtdtOffset = FdtAcpiAlign(MadtOffset + FdtAcpiMadtLength(Platform));
    ULONG SpcrOffset = FdtAcpiAlign(GtdtOffset + (Platform->TimerPresent ? sizeof(ARM64_ACPI_GTDT) : 0));
    ULONG DsdtOffset = FdtAcpiAlign(SpcrOffset + (Platform->UartPresent ? sizeof(ARM64_ACPI_SPCR) : 0));
    ULONG DsdtLength = sizeof(DESCRIPTION_HEADER) + FdtAcpiAmlWriteDefinitionBlock(NULL, Platform->CpuCount);
    ULONG Total = DsdtOffset + DsdtLength;
    PUCHAR Base;
    PRSDP Rsdp;
    PXSDT Xsdt;
    PDESCRIPTION_HEADER Dsdt;
    ULONG Entry = 0;

    Status = GlobalSystemTable->BootServices->AllocatePages(AllocateMaxAddress,
                                                            EfiACPIReclaimMemory,
                                                            EFI_SIZE_TO_PAGES(Total),
                                                            &Memory);
    if (EFI_ERROR(Status))
    {
        ERR("ARM64: no memory for ACPI tables (0x%llx)\n", (ULONGLONG)Status);
        return FALSE;
    }

    Base = (PUCHAR)(ULONG_PTR)Memory;
    RtlZeroMemory(Base, EFI_SIZE_TO_PAGES(Total) * EFI_PAGE_SIZE);

    Dsdt = (PDESCRIPTION_HEADER)(Base + DsdtOffset);
    FdtAcpiFillHeader(Dsdt, DSDT_SIGNATURE, DsdtLength, 2);
    FdtAcpiAmlWriteDefinitionBlock((PUCHAR)(Dsdt + 1), Platform->CpuCount);
    FdtAcpiSealTable(Dsdt);

    FdtAcpiBuildFadt(Platform, (PFADT)(Base + FadtOffset), Memory + DsdtOffset);
    FdtAcpiSealTable((PDESCRIPTION_HEADER)(Base + FadtOffset));

    FdtAcpiBuildMadt(Platform, (PARM64_ACPI_MADT)(Base + MadtOffset));
    FdtAcpiSealTable((PDESCRIPTION_HEADER)(Base + MadtOffset));

    Xsdt = (PXSDT)(Base + XsdtOffset);
    FdtAcpiFillHeader(&Xsdt->Header, XSDT_SIGNATURE, XsdtLength, 1);
    Xsdt->Tables[Entry++].QuadPart = (LONGLONG)(Memory + FadtOffset);
    Xsdt->Tables[Entry++].QuadPart = (LONGLONG)(Memory + MadtOffset);

    if (Platform->TimerPresent)
    {
        FdtAcpiBuildGtdt(Platform, (PARM64_ACPI_GTDT)(Base + GtdtOffset));
        FdtAcpiSealTable((PDESCRIPTION_HEADER)(Base + GtdtOffset));
        Xsdt->Tables[Entry++].QuadPart = (LONGLONG)(Memory + GtdtOffset);
    }

    if (Platform->UartPresent)
    {
        FdtAcpiBuildSpcr(Platform, (PARM64_ACPI_SPCR)(Base + SpcrOffset));
        FdtAcpiSealTable((PDESCRIPTION_HEADER)(Base + SpcrOffset));
        Xsdt->Tables[Entry++].QuadPart = (LONGLONG)(Memory + SpcrOffset);
    }

    FdtAcpiSealTable(&Xsdt->Header);

    Rsdp = (PRSDP)(Base + RsdpOffset);
    Rsdp->Signature = RSDP_SIGNATURE;
    RtlCopyMemory(Rsdp->OEMID, "LBRNT ", sizeof(Rsdp->OEMID));
    Rsdp->Revision = 2;
    Rsdp->Length = sizeof(RSDP);
    Rsdp->XsdtAddress.QuadPart = (LONGLONG)(Memory + XsdtOffset);
    Rsdp->Checksum = FdtAcpiChecksum(Rsdp, FIELD_OFFSET(RSDP, Length));
    Rsdp->XChecksum = FdtAcpiChecksum(Rsdp, sizeof(RSDP));

    Status = GlobalSystemTable->BootServices->InstallConfigurationTable(&Acpi20Guid, Rsdp);
    if (EFI_ERROR(Status))
    {
        ERR("ARM64: the ACPI tables could not be installed (0x%llx)\n", (ULONGLONG)Status);
        GlobalSystemTable->BootServices->FreePages(Memory, EFI_SIZE_TO_PAGES(Total));
        return FALSE;
    }

    return TRUE;
}

static
const VOID *
FdtAcpiFindFirmwareDeviceTree(VOID)
{
    EFI_GUID DeviceTreeGuid = FDT_TABLE_GUID;
    EFI_GUID Acpi20Guid = EFI_ACPI_20_TABLE_GUID;
    EFI_GUID Acpi10Guid = ACPI_10_TABLE_GUID;
    const VOID *DeviceTree = NULL;
    UINTN Index;

    for (Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; Index++)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        if (!memcmp(&Entry->VendorGuid, &Acpi20Guid, sizeof(EFI_GUID)) ||
            !memcmp(&Entry->VendorGuid, &Acpi10Guid, sizeof(EFI_GUID)))
        {
            return NULL;
        }

        if (!memcmp(&Entry->VendorGuid, &DeviceTreeGuid, sizeof(EFI_GUID)))
            DeviceTree = Entry->VendorTable;
    }

    return DeviceTree;
}

VOID
Arm64DescribeDeviceTreePlatform(VOID)
{
    FDTACPI_PLATFORM Platform;
    const VOID *DeviceTree;

    if (!GlobalSystemTable || !GlobalSystemTable->ConfigurationTable)
        return;

    DeviceTree = FdtAcpiFindFirmwareDeviceTree();
    if (!DeviceTree)
        return;

    RtlZeroMemory(&Platform, sizeof(Platform));
    if (!RiscvFdtOpen(DeviceTree, RISCV_FDT_MAXIMUM_SIZE, &Platform.Fdt))
    {
        ERR("ARM64: the firmware device tree is not valid\n");
        return;
    }

    Platform.Root = RiscvFdtRootNode(&Platform.Fdt);
    if (Platform.Root == RISCV_FDT_NO_NODE ||
        !FdtAcpiDescribeCpus(&Platform) ||
        !FdtAcpiDescribeGic(&Platform))
    {
        return;
    }

    FdtAcpiDescribePsci(&Platform);
    FdtAcpiDescribeTimer(&Platform);
    FdtAcpiDescribeUart(&Platform);

    if (!FdtAcpiInstallTables(&Platform))
        return;

    TRACE("ARM64: ACPI tables built from the device tree: %lu CPUs, GICv%lu, PSCI %s, timer %s, UART %s\n",
          Platform.CpuCount,
          Platform.GicVersion,
          Platform.PsciCompliant ? (Platform.PsciUseHvc ? "hvc" : "smc") : "none",
          Platform.TimerPresent ? "yes" : "no",
          Platform.UartPresent ? "yes" : "no");
}
