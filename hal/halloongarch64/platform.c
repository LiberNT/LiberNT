/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Fixed platform description and direct-memory-window bookkeeping
 *
 * This platform has no firmware device description to read: the LoongArch
 * loader block (sdk/include/reactos/arc/arc.h) carries no FDT pointer, and
 * bootloader/arch/loongarch64/winldr.c leaves EarlyConsole* and
 * EarlyDeviceRanges zeroed. There is consequently no ACPI table and no
 * discovery path in this HAL. Instead the HAL carries one compile-time
 * description of the machine it targets and proves at boot that the machine is
 * that machine by reading a register only that machine's bridge has.
 *
 * The addresses are the QEMU LoongArch virt board's, from
 * qemu/hw/loongarch/virt.c and include/hw/loongarch/virt.h:
 *
 *   VIRT_PCH_REG_BASE   0x10000000   size 0x400    (7A1000-style PCH-PIC)
 *   VIRT_PCH_MSI_ADDR   0x2ff00000   size 0x8
 *   VIRT_PCI_CFG_BASE   0x20000000   size 0x08000000 (128 buses x 1 MiB)
 *   VIRT_PCI_IO_BASE    0x18004000   size 0xC000
 *   VIRT_PCI_MEM_BASE   0x40000000   size 0x40000000
 *   VIRT_UART_BASE      0x1fe001e0   size 0x100, four instances
 *   VIRT_RTC_REG_BASE   0x100d0100   size 0x100  (ls7a_rtc)
 *
 * and the interrupt wiring of the same file: UART n is PCH-PIC input 2+n,
 * the RTC is input 6, and PCIe INTA..INTD are inputs 16..19. The PCH-PIC
 * outputs 0..31 feed EIOINTC inputs 0..31, and PCH-MSI supplies 32..255.
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

/* CPUCFG3 bit 3, [V1-4.2.6.5] CPUCFG3 definition: "1 indicates support for
 * hardware Cache coherent DMA". */
#define HALP_LA64_CPUCGF3_CCDMA 0x00000008ULL

/* CPUCFG4/5, [V1-4.2.6.5]: CC_FREQ in 31:0, CC_MUL in 15:0 and CC_DIV in
 * 31:16 of CPUCFG5. The constant timer runs at CC_FREQ * CC_MUL / CC_DIV. */
#define HALP_LA64_CPUCGF5_MUL_MASK 0x0000FFFFULL
#define HALP_LA64_CPUCGF5_DIV_SHIFT 16
#define HALP_LA64_CPUCGF5_DIV_MASK 0x0000FFFFULL

static const HALP_LA64_PLATFORM HalpLa64QemuVirt =
{
    HALP_LA64_PLATFORM_QEMU_VIRT,
    0x10000000ULL, 0x400,          /* PCH-PIC */
    0x2FF00000ULL, 0x8,            /* PCH-MSI */
    0, 0,                          /* no LIOINTC on QEMU virt */
    0x20000000ULL, 0x08000000ULL,  /* ECAM, 128 buses */
    0x18004000ULL, 0xC000,         /* PCI I/O, MMIO-mapped */
    0x40000000ULL, 0x40000000ULL,  /* PCI memory */
    0, 127, 0,                     /* buses, segment */
    16,                            /* PCIe INTA = PCH-PIC input 16 */
    0x1FE001E0ULL, 0x100, 4, 2,    /* UART(s), first IRQ */
    0x100D0100ULL, 0x100, 6,       /* RTC, IRQ */
    FALSE                          /* filled from CPUCFG3 */
};

HALP_LA64_PLATFORM HalpLa64Platform;
ULONG64 HalpLa64TimerFrequency;
ULONG64 HalpLa64BootCpuId;
ULONG64 HalpLa64CpuCfg1;
ULONG64 HalpLa64CpuCfg2;
ULONG64 HalpLa64CpuCfg3;
ULONG64 HalpLa64DirectMapBase;
ULONG64 HalpLa64UncachedBase;
ULONG64 HalpLa64WriteCombineBase;
ULONG64 HalpLa64PhysicalLimit;

static
ULONG64
HalpLa64ReadCpucfg(
    _In_ ULONG Number)
{
    ULONG64 Value;

    __asm__ __volatile__("cpucfg %0, %1" : "=r"(Value) : "r"((ULONG)Number));
    return Value;
}

ULONG64
HalpLa64ReadTime(VOID)
{
    ULONG64 Counter, TimerId;

    /* RDTIME.D rd, rj: rd = stable counter, rj = timer identity
     * [V1-2.2.10.4]. The counter is consistent on every core of the machine
     * and is not writable, so it is both the QPC source and the clock's
     * time base. */
    __asm__ __volatile__("rdtime.d %0, %1" : "=r"(Counter), "=r"(TimerId));
    UNREFERENCED_PARAMETER(TimerId);
    return Counter;
}

BOOLEAN
HalpLa64ReadTimerFrequency(
    _Out_ PULONG64 Frequency)
{
    ULONG64 Crystal, Scale;

    if (!Frequency)
        return FALSE;
    Crystal = HalpLa64ReadCpucfg(4);
    Scale = HalpLa64ReadCpucfg(5);
    if (!Crystal)
        return FALSE;
    /* CC_MUL/CC_DIV scale the crystal frequency; both read as 1 on hardware
     * that has no scaling, and a zero field is treated as 1 because the
     * manual defines the field as a multiplier, not as "absent". */
    {
        ULONG64 Multiply = Scale & HALP_LA64_CPUCGF5_MUL_MASK;
        ULONG64 Divide = (Scale >> HALP_LA64_CPUCGF5_DIV_SHIFT) &
                         HALP_LA64_CPUCGF5_DIV_MASK;

        if (!Multiply)
            Multiply = 1;
        if (!Divide)
            Divide = 1;
        if (Crystal > MAXULONGLONG / Multiply)
            return FALSE;
        *Frequency = Crystal * Multiply / Divide;
    }
    return (*Frequency != 0);
}

/*
 * Decode the three direct-mapping windows the loader already programmed. The
 * loader block carries both the raw DMW values and the three VA bases; the
 * values are checked against the bases so a mismatch is a loader contract
 * failure rather than a silent wrong address.
 */
static
BOOLEAN
HalpLa64AdoptDirectMap(
    _In_ const LOONGARCH64_LOADER_BLOCK *Block)
{
    ULONG Index;

    for (Index = 0; Index < 4; ++Index)
    {
        ULONG64 Value = Block->Dmw[Index];
        ULONG64 Base;

        if (!(Value & 0x1)) /* PLV0 bit; an unused window reads as zero */
            continue;
        Base = Value & 0xE0000000ULL; /* VSEG 31:29, [V1-7.5.18] */
        switch (Base)
        {
            case 0x80000000ULL:
                if (Block->UncachedBase != 0x8000000000000000ULL)
                    return FALSE;
                break;
            case 0x90000000ULL:
                if (Block->DirectMapBase != 0x9000000000000000ULL)
                    return FALSE;
                break;
            case 0xA0000000ULL:
                if (Block->WriteCombineBase != 0xA000000000000000ULL)
                    return FALSE;
                break;
            default:
                return FALSE;
        }
    }
    if (Block->DirectMapBase != LOONGARCH64_LOADER_DIRECT_MAP_BASE ||
        Block->UncachedBase != LOONGARCH64_LOADER_UNCACHED_BASE ||
        Block->WriteCombineBase != LOONGARCH64_LOADER_WRITE_COMBINE_BASE ||
        Block->PhysicalLimit != LOONGARCH64_LOADER_PHYSICAL_LIMIT)
        return FALSE;

    HalpLa64DirectMapBase = Block->DirectMapBase;
    HalpLa64UncachedBase = Block->UncachedBase;
    HalpLa64WriteCombineBase = Block->WriteCombineBase;
    HalpLa64PhysicalLimit = Block->PhysicalLimit;
    return TRUE;
}

BOOLEAN
HalpLa64AdoptLoaderBlock(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    const LOONGARCH64_LOADER_BLOCK *Block = &LoaderBlock->u.LoongArch64;

    if ((Block->Version != LOONGARCH64_LOADER_BLOCK_VERSION) ||
        (Block->Size != sizeof(*Block)) ||
        ((Block->Flags & LOONGARCH64_LOADER_REQUIRED_FLAGS) !=
         LOONGARCH64_LOADER_REQUIRED_FLAGS) ||
        (Block->Flags &
         ~(LOONGARCH64_LOADER_REQUIRED_FLAGS | LOONGARCH64_LOADER_OPTIONAL_FLAGS)) ||
        (Block->PageDirectoryRoot == 0) ||
        (Block->PhysicalLimit != LOONGARCH64_LOADER_PHYSICAL_LIMIT))
        return FALSE;

    HalpLa64BootCpuId = Block->CpuId;
    HalpLa64CpuCfg1 = Block->CpuCfg1;
    HalpLa64CpuCfg2 = Block->CpuCfg2;
    HalpLa64CpuCfg3 = HalpLa64ReadCpucfg(3);

    /* The loader's CPUCFG1 copy must agree with the live register about the
     * virtual address width; if it does not, the handoff copied a different
     * core's state and no address the kernel builds is trustworthy. */
    if ((Block->CpuCfg1 & 0xFF0ULL) != (HalpLa64ReadCpucfg(1) & 0xFF0ULL))
        return FALSE;
    return HalpLa64AdoptDirectMap(Block);
}

BOOLEAN
HalpLa64SelectPlatform(VOID)
{
    HalpLa64Platform = HalpLa64QemuVirt;
    HalpLa64Platform.DmaCoherent = (HalpLa64CpuCfg3 & HALP_LA64_CPUCGF3_CCDMA) != 0;
    return TRUE;
}

ULONG
HalpLa64IrqlForVector(
    _In_ ULONG Vector)
{
    /* EIOINTC groups 32 sources per hardware line and the HAL programs group
     * n to HWI n, so the source number fixes the line and therefore the IRQL
     * ([V1-6.1.2] is satisfied by construction). The LIOINTC has 32 sources
     * and four parent lines, and HalpLa64EnableControllerSource gives each
     * source group of eight one parent, so the same rule applies with a
     * group size of eight. */
    if (Vector < HALP_LA64_EIOINTC_SOURCES)
        return HALP_LA64_IRQL_HWI(min(Vector / 32, HALP_LA64_HWI_COUNT - 1));
    if (HalpLa64Platform.LioIntcBase &&
        Vector >= HALP_LA64_LIOINTC_VECTOR_BASE &&
        Vector < HALP_LA64_LIOINTC_VECTOR_BASE + HALP_LA64_LIOINTC_SOURCES)
    {
        ULONG GroupSize = HALP_LA64_LIOINTC_SOURCES / HALP_LA64_LIOINTC_CORES;

        return HALP_LA64_IRQL_HWI((Vector - HALP_LA64_LIOINTC_VECTOR_BASE) /
                                  GroupSize);
    }
    return HALP_LA64_EXTERNAL_IRQL;
}

BOOLEAN
HalpLa64ValidVector(
    _In_ ULONG Vector)
{
    return (Vector < HALP_LA64_EIOINTC_SOURCES) ||
           (HalpLa64Platform.LioIntcBase &&
            Vector >= HALP_LA64_LIOINTC_VECTOR_BASE &&
            Vector < HALP_LA64_LIOINTC_VECTOR_BASE + HALP_LA64_LIOINTC_SOURCES);
}
