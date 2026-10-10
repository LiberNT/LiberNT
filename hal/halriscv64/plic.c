/*
 * PROJECT:     LiberNT RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     QEMU virt supervisor PLIC discovery and MMIO ownership
 */

#include <ntifs.h>
#include <reactos/riscv64/fdtlib.h>
#include "halp.h"

#define RISCV_PLIC_ENABLE_BASE       0x2000UL
#define RISCV_PLIC_ENABLE_STRIDE     0x80UL
#define RISCV_PLIC_CONTEXT_BASE      0x200000UL
#define RISCV_PLIC_CONTEXT_STRIDE    0x1000UL
#define RISCV_PLIC_CONTEXT_REGISTERS 8UL
#define RISCV_PLIC_THRESHOLD_OFFSET  0UL
#define RISCV_PLIC_CLAIM_OFFSET      4UL
#define RISCV_PLIC_MAX_SOURCE        1023UL
#define RISCV_PLIC_PHYSICAL_LIMIT    0x0100000000000000ULL

typedef struct _RISCV_PLIC
{
    ULONG64 PhysicalAddress;
    ULONG64 Size;
    ULONG64 MappedSize;
    ULONG SourceCount;
    ULONG Phandle;
    ULONG SupervisorContext;
    volatile UCHAR *Mapping;
    BOOLEAN Present;
} RISCV_PLIC;

static RISCV_PLIC HalpRiscvPlic;
static KSPIN_LOCK HalpRiscvPlicEnableLock;

/* Only the boot hart's supervisor context is owned by this uniprocessor HAL. */
static BOOLEAN
HalpRiscvFindSupervisorCpuIntc(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG64 BootHartId,
    _Out_ PULONG Phandle)
{
    ULONG Cpus, Cpu, Intc, Length, Hart, Cells;
    const VOID *Property;

    if (BootHartId > MAXULONG ||
        !RiscvFdtFindNode(Fdt, "/cpus", &Cpus, NULL) ||
        !RiscvFdtReadU32(Fdt, Cpus, "#address-cells", &Cells) || Cells != 1 ||
        !RiscvFdtReadU32(Fdt, Cpus, "#size-cells", &Cells) || Cells != 0)
        return FALSE;

    for (Cpu = RiscvFdtFirstChild(Fdt, Cpus);
         Cpu != RISCV_FDT_NO_NODE;
         Cpu = RiscvFdtNextSibling(Fdt, Cpu))
    {
        Property = RiscvFdtGetProperty(Fdt, Cpu, "device_type", &Length);
        if (!RiscvFdtStringListContains(Property, Length, "cpu"))
            continue;
        if (!RiscvFdtReadU32(Fdt, Cpu, "reg", &Hart) || Hart != BootHartId)
            continue;

        for (Intc = RiscvFdtFirstChild(Fdt, Cpu);
             Intc != RISCV_FDT_NO_NODE;
             Intc = RiscvFdtNextSibling(Fdt, Intc))
        {
            Property = RiscvFdtGetProperty(Fdt, Intc, "compatible", &Length);
            if (!RiscvFdtStringListContains(Property, Length, "riscv,cpu-intc"))
                continue;
            if (!RiscvFdtReadU32(Fdt, Intc, "#interrupt-cells", &Cells) || Cells != 1 ||
                !RiscvFdtReadU32(Fdt, Intc, "phandle", Phandle) ||
                *Phandle == 0 || *Phandle == MAXULONG)
                return FALSE;
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

BOOLEAN
HalpRiscvInitializePlic(
    _In_reads_bytes_(DeviceTreeSize) const VOID *DeviceTree,
    _In_ SIZE_T DeviceTreeSize,
    _In_ ULONG64 BootHartId)
{
    RISCV_FDT Fdt;
    ULONG Root, Soc, Parent, Node, Length, CpuPhandle, Index, PairCount, Value, Phandle;
    ULONG SupervisorContext = MAXULONG;
    ULONG64 Address, Size;
    const VOID *Property;

    if (!RiscvFdtOpen(DeviceTree, DeviceTreeSize, &Fdt))
        return FALSE;
    Root = RiscvFdtRootNode(&Fdt);
    if (!RiscvFdtFindNode(&Fdt, "/soc", &Soc, &Parent) || Parent != Root)
        return TRUE;

    for (Node = RiscvFdtFirstChild(&Fdt, Soc);
         Node != RISCV_FDT_NO_NODE;
         Node = RiscvFdtNextSibling(&Fdt, Node))
    {
        Property = RiscvFdtGetProperty(&Fdt, Node, "compatible", &Length);
        if (!RiscvFdtStringListContains(Property, Length, "sifive,plic-1.0.0") &&
            !RiscvFdtStringListContains(Property, Length, "riscv,plic0"))
            continue;
        if (HalpRiscvPlic.Present ||
            !HalpRiscvFindSupervisorCpuIntc(&Fdt, BootHartId, &CpuPhandle))
            return FALSE;

        Property = RiscvFdtGetProperty(&Fdt, Node, "reg", &Length);
        if (!Property || Length != 4 * sizeof(ULONG) ||
            !RiscvFdtReadCells(Property, Length, 0, 2, &Address) ||
            !RiscvFdtReadCells(Property, Length, 2, 2, &Size) ||
            !Size || (Address & (PAGE_SIZE - 1)) || (Size & (PAGE_SIZE - 1)) ||
            Address >= RISCV_PLIC_PHYSICAL_LIMIT ||
            Size > RISCV_PLIC_PHYSICAL_LIMIT - Address ||
            !RiscvFdtReadU32(&Fdt, Node, "riscv,ndev", &Value) ||
            Value == 0 || Value > RISCV_PLIC_MAX_SOURCE)
            return FALSE;

        Property = RiscvFdtGetProperty(&Fdt, Node, "interrupts-extended", &Length);
        if (!Property || !Length || (Length % (2 * sizeof(ULONG))))
            return FALSE;
        PairCount = Length / (2 * sizeof(ULONG));
        for (Index = 0; Index < PairCount; ++Index)
        {
            ULONG Phandle = RiscvFdtReadBigEndian32((const UCHAR *)Property + Index * 2 * sizeof(ULONG));
            ULONG Cause = RiscvFdtReadBigEndian32((const UCHAR *)Property + (Index * 2 + 1) * sizeof(ULONG));

            if (Phandle == CpuPhandle && Cause == 9)
            {
                if (SupervisorContext != MAXULONG)
                    return FALSE;
                SupervisorContext = Index;
            }
        }
        if (SupervisorContext == MAXULONG ||
            SupervisorContext > (MAXULONG - RISCV_PLIC_CONTEXT_BASE - RISCV_PLIC_CONTEXT_REGISTERS) /
                                    RISCV_PLIC_CONTEXT_STRIDE ||
            RISCV_PLIC_CONTEXT_BASE + SupervisorContext * RISCV_PLIC_CONTEXT_STRIDE +
                RISCV_PLIC_CONTEXT_REGISTERS > Size ||
            RISCV_PLIC_ENABLE_BASE + SupervisorContext * RISCV_PLIC_ENABLE_STRIDE +
                ((Value + 32) / 32) * sizeof(ULONG) > Size)
            return FALSE;

        if (!RiscvFdtReadU32(&Fdt, Node, "phandle", &Phandle) || !Phandle ||
            Phandle == MAXULONG ||
            !RiscvFdtReadU32(&Fdt, Node, "#interrupt-cells", &Index) || Index != 1)
            return FALSE;

        HalpRiscvPlic.PhysicalAddress = Address;
        HalpRiscvPlic.Size = Size;
        HalpRiscvPlic.MappedSize = min(Size, RISCV_PLIC_CONTEXT_BASE +
                                             ((ULONG64)SupervisorContext + 1) * RISCV_PLIC_CONTEXT_STRIDE);
        HalpRiscvPlic.SourceCount = Value;
        HalpRiscvPlic.Phandle = Phandle;
        HalpRiscvPlic.SupervisorContext = SupervisorContext;
        HalpRiscvPlic.Present = TRUE;
    }
    return TRUE;
}

BOOLEAN
HalpRiscvMapPlic(VOID)
{
    PHYSICAL_ADDRESS Address;
    ULONG Word;

    if (!HalpRiscvPlic.Present)
        return TRUE;
    Address.QuadPart = HalpRiscvPlic.PhysicalAddress;
    HalpRiscvPlic.Mapping = MmMapIoSpace(Address, HalpRiscvPlic.MappedSize, MmNonCached);
    if (!HalpRiscvPlic.Mapping)
        return FALSE;

    /* The boot hart owns only its supervisor context. Start with every
     * source disabled; each connected interrupt enables its own source. */
    for (Word = 0; Word <= HalpRiscvPlic.SourceCount / 32; ++Word)
    {
        *(volatile ULONG *)(HalpRiscvPlic.Mapping + RISCV_PLIC_ENABLE_BASE +
                             HalpRiscvPlic.SupervisorContext * RISCV_PLIC_ENABLE_STRIDE +
                             Word * sizeof(ULONG)) = 0;
    }
    *(volatile ULONG *)(HalpRiscvPlic.Mapping + RISCV_PLIC_CONTEXT_BASE +
                         HalpRiscvPlic.SupervisorContext * RISCV_PLIC_CONTEXT_STRIDE +
                         RISCV_PLIC_THRESHOLD_OFFSET) = 0;
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    return TRUE;
}

BOOLEAN
HalpRiscvPlicHasSource(ULONG Phandle, ULONG Source)
{
    return HalpRiscvPlic.Present && Phandle == HalpRiscvPlic.Phandle &&
           Source != 0 && Source <= HalpRiscvPlic.SourceCount;
}

BOOLEAN
HalpRiscvPlicValidSource(ULONG Source)
{
    return HalpRiscvPlic.Present && Source != 0 &&
           Source <= HalpRiscvPlic.SourceCount;
}

static volatile ULONG *
HalpRiscvPlicEnableWord(ULONG Source)
{
    return (volatile ULONG *)(HalpRiscvPlic.Mapping + RISCV_PLIC_ENABLE_BASE +
                              HalpRiscvPlic.SupervisorContext * RISCV_PLIC_ENABLE_STRIDE +
                              (Source / 32) * sizeof(ULONG));
}

static KIRQL
HalpRiscvPlicLockEnable(VOID)
{
    KIRQL OldIrql;

    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&HalpRiscvPlicEnableLock);
    return OldIrql;
}

static VOID
HalpRiscvPlicUnlockEnable(KIRQL OldIrql)
{
    KeReleaseSpinLockFromDpcLevel(&HalpRiscvPlicEnableLock);
    KeLowerIrql(OldIrql);
}

BOOLEAN
NTAPI
HalEnableSystemInterrupt(ULONG Vector, KIRQL Irql, KINTERRUPT_MODE Mode)
{
    volatile ULONG *Enable;
    KIRQL OldIrql;
    ULONG Bit;

    if (!HalpRiscvPlic.Mapping || !HalpRiscvPlicValidSource(Vector) ||
        Irql != RISCV_HAL_EXTERNAL_IRQL || Mode != LevelSensitive)
        return FALSE;

    Enable = HalpRiscvPlicEnableWord(Vector);
    Bit = 1UL << (Vector % 32);
    OldIrql = HalpRiscvPlicLockEnable();
    *(volatile ULONG *)(HalpRiscvPlic.Mapping + Vector * sizeof(ULONG)) = 1;
    *Enable |= Bit;
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    HalpRiscvPlicUnlockEnable(OldIrql);
    KiRiscvSetInterruptEnabled(RISCV_HAL_SIE_SEIE, TRUE);
    return TRUE;
}

VOID
NTAPI
HalDisableSystemInterrupt(ULONG Vector, KIRQL Irql)
{
    volatile ULONG *Enable;
    KIRQL OldIrql;
    ULONG Bit;

    if (!HalpRiscvPlic.Mapping || !HalpRiscvPlicValidSource(Vector) ||
        Irql != RISCV_HAL_EXTERNAL_IRQL)
        return;
    Enable = HalpRiscvPlicEnableWord(Vector);
    Bit = 1UL << (Vector % 32);
    OldIrql = HalpRiscvPlicLockEnable();
    *Enable &= ~Bit;
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    *(volatile ULONG *)(HalpRiscvPlic.Mapping + Vector * sizeof(ULONG)) = 0;
    HalpRiscvPlicUnlockEnable(OldIrql);
}

ULONG
NTAPI
HalpRiscvClaimPlicInterrupt(VOID)
{
    ULONG Source;

    if (!HalpRiscvPlic.Mapping)
        return 0;
    Source = *(volatile ULONG *)(HalpRiscvPlic.Mapping + RISCV_PLIC_CONTEXT_BASE +
                                 HalpRiscvPlic.SupervisorContext * RISCV_PLIC_CONTEXT_STRIDE +
                                 RISCV_PLIC_CLAIM_OFFSET);
    if (Source)
        HalpRiscvAcknowledgePciInterrupt(Source);
    return Source;
}

VOID
NTAPI
HalpRiscvCompletePlicInterrupt(ULONG Source)
{
    volatile ULONG *Complete, *Enable;
    KIRQL OldIrql;
    ULONG Bit;

    if (!HalpRiscvPlic.Mapping || !HalpRiscvPlicValidSource(Source))
        return;
    Complete = (volatile ULONG *)(HalpRiscvPlic.Mapping + RISCV_PLIC_CONTEXT_BASE +
                                   HalpRiscvPlic.SupervisorContext * RISCV_PLIC_CONTEXT_STRIDE +
                                   RISCV_PLIC_CLAIM_OFFSET);
    Enable = HalpRiscvPlicEnableWord(Source);
    Bit = 1UL << (Source % 32);
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    OldIrql = HalpRiscvPlicLockEnable();
    if (*Enable & Bit)
    {
        *Complete = Source;
    }
    else
    {
        *Enable |= Bit;
        __asm__ __volatile__("fence iorw, iorw" ::: "memory");
        *Complete = Source;
        __asm__ __volatile__("fence iorw, iorw" ::: "memory");
        *Enable &= ~Bit;
    }
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    HalpRiscvPlicUnlockEnable(OldIrql);
}
