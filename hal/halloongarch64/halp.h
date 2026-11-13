/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Private bootstrap platform contracts
 *
 * Every architectural constant below is read from the LoongArch Volume 1
 * manual (local mirror: /mnt/data/la64-dev/docs/loongarch-docs/
 * LoongArch-Vol1-EN.html) and is tagged with the section it comes from.
 * Board addresses come from the emulated board's own source
 * (qemu/hw/loongarch/virt.c + include/hw/loongarch/virt.h) and each one says
 * so; the HAL never infers a board address from an architectural rule.
 *
 * The kernel keeps its own copy of the CSR numbers in
 * ntos/include/internal/loongarch64/ke.h. The HAL is a separate image and
 * must not include kernel-private headers, so the numbers are repeated here
 * under HALP_LA64_* names. Both sides cite the same manual tables.
 */

#pragma once

#include <ndk/ketypes.h>
#include "../arch/common/include/halsecondary.h"

/* ---------------------------------------------------------------------------
 * CSRs
 * [V1-7.4.x] basic CSRs, [V1-7.5.x] mapped-translation CSRs,
 * [V1-7.6.x] timer CSRs.
 * ------------------------------------------------------------------------ */

#define HALP_LA64_CSR_CRMD      0x00    /* [V1-7.4.1] */
#define HALP_LA64_CSR_PRMD      0x01    /* [V1-7.4.2] */
#define HALP_LA64_CSR_EUEN      0x02    /* [V1-7.4.3] */
#define HALP_LA64_CSR_ECFG      0x04    /* [V1-7.4.5] */
#define HALP_LA64_CSR_ESTAT     0x05    /* [V1-7.4.6] */
#define HALP_LA64_CSR_ERA       0x06    /* [V1-7.4.7] */
#define HALP_LA64_CSR_BADV      0x07    /* [V1-7.4.8] */
#define HALP_LA64_CSR_EENTRY    0x0C    /* [V1-7.4.9] */
#define HALP_LA64_CSR_CPUID     0x20    /* [V1-7.4.15] */
#define HALP_LA64_CSR_TID       0x40    /* [V1-7.6.1] */
#define HALP_LA64_CSR_TCFG      0x41    /* [V1-7.6.2] */
#define HALP_LA64_CSR_TVAL      0x42    /* [V1-7.6.3] */
#define HALP_LA64_CSR_CNTC      0x43    /* [V1-7.6.4] */
#define HALP_LA64_CSR_TICLR     0x44    /* [V1-7.6.5] */
#define HALP_LA64_CSR_DMW0      0x180   /* [V1-7.5.18] */
#define HALP_LA64_CSR_DMW1      0x181
#define HALP_LA64_CSR_DMW2      0x182
#define HALP_LA64_CSR_DMW3      0x183

/* CRMD [V1-7.4.1]: PLV 1:0 (0 kernel, 3 user), IE 2, DA 3, PG 4. */
#define HALP_LA64_CRMD_IE       0x00000004u
#define HALP_LA64_CRMD_DA       0x00000008u
#define HALP_LA64_CRMD_PG       0x00000010u

/* ECFG [V1-7.4.5]: LIE 12:0, one bit per line interrupt. */
#define HALP_LA64_ECFG_LIE_MASK 0x00001FFFu

/* ESTAT [V1-7.4.6]: IS 12:0, Ecode 21:16. */
#define HALP_LA64_ESTAT_IS_MASK 0x00001FFFu
#define HALP_LA64_ESTAT_ECODE_SHIFT 16
#define HALP_LA64_ESTAT_ECODE_MASK  0x0000003Fu
#define HALP_LA64_ESTAT_ECODE_PME   0x04u   /* [V1-6.1.1] page modification */

/* TCFG [V1-7.6.2]: En 0, Periodic 1, InitVal n-1:2. The manual states the
 * initial value must be a multiple of 4 and that the hardware appends two
 * zero bits before use, so writing the tick period P into TCFG[31:2] (the
 * bits P already occupies) programs a countdown of P when P is 4-aligned. */
#define HALP_LA64_TCFG_EN       0x00000001u
#define HALP_LA64_TCFG_PERIODIC 0x00000002u

/* TICLR [V1-7.6.5]: CLR 0, write 1 clears the timer interrupt. */
#define HALP_LA64_TICLR_CLR     0x00000001u

/* Line interrupt numbers [V1-6.1.1]: SWI0/1 = 0/1, HWI0..HWI7 = 2..9,
 * PMCOV = 10, TI = 11, IPI = 12. [V1-6.1.2] fixes the priority order as the
 * numeric order, so the HAL hands out IRQLs monotonically in this number. */
#define HALP_LA64_IRQ_SWI0      0
#define HALP_LA64_IRQ_SWI1      1
#define HALP_LA64_IRQ_HWI0      2
#define HALP_LA64_IRQ_HWI(line) (2 + (line))
#define HALP_LA64_IRQ_PMCOV     10
#define HALP_LA64_IRQ_TIMER     11
#define HALP_LA64_IRQ_IPI       12
#define HALP_LA64_IRQ_BIT(line) (1u << (line))
#define HALP_LA64_HWI_COUNT     8

/* IRQL policy. [V1-6.1.2] only requires that a higher int number is a higher
 * priority; the assignment itself is HAL policy. HWI0..HWI7 (int number 2..9)
 * take the eight device levels 4..11, which leaves SYNCH_LEVEL 12, CLOCK_LEVEL
 * 13 and IPI_LEVEL 14 to the kernel (ntos/include/internal/loongarch64/ke.h
 * fixes SYNCH_LEVEL = 12). */
#define HALP_LA64_IRQL_HWI0     4
#define HALP_LA64_IRQL_HWI(line) (4 + (line))
#define HALP_LA64_EXTERNAL_IRQL 4

/* ---------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------ */

/* The same increment window the other NT ports use: 10000 (1 ms) to 156250
 * (15.625 ms) in 100 ns units. */
#define HALP_LA64_MAXIMUM_INCREMENT 156250UL
#define HALP_LA64_MINIMUM_INCREMENT 10000UL
#define HALP_LA64_DEFAULT_PROFILE_INTERVAL 100000UL /* 10 ms */

/* ---------------------------------------------------------------------------
 * Loongson interrupt-controller windows
 *
 * EXTIOI/EIOINTC and the IPI mailbox are IOCSR-space devices, addressed with
 * iocsrrd/iocsrwr [V1-4.2.2], not MMIO. The offsets are the ones Loongson's
 * own Linux irqchip drivers program (drivers/irqchip/irq-loongson-eiointc.c,
 * drivers/irqchip/irq-loongson-pch-pic.c) and match QEMU's models
 * (hw/intc/loongarch_extioi.c, hw/intc/loongson_ipi_common.c).
 * ------------------------------------------------------------------------ */

/* EIOINTC: 256 sources, IOCSR base 0x1400 (QEMU APIC_BASE = 0x1000 + 0x400).
 * Linux names the same registers by their absolute IOCSR address. */
#define HALP_LA64_EIOINTC_BASE          0x1400UL
#define HALP_LA64_EIOINTC_NODEMAP       0x14A0UL
#define HALP_LA64_EIOINTC_IPMAP         0x14C0UL  /* 8 groups -> 8 HWI pins */
#define HALP_LA64_EIOINTC_ENABLE        0x1600UL  /* 8 x 32-bit words */
#define HALP_LA64_EIOINTC_BOUNCE        0x1680UL
#define HALP_LA64_EIOINTC_COREISR       0x1800UL  /* 8 x 32-bit words, own CPU */
#define HALP_LA64_EIOINTC_ROUTE         0x1C00UL  /* 1 byte per source */
#define HALP_LA64_EIOINTC_SOURCES       256UL
#define HALP_LA64_EIOINTC_GROUPS        8UL

/* IPI mailbox IOCSR block [Loongson driver, loongson_ipi_common.h]. The
 * block is per-CPU: every CPU reaches its own status registers at the same
 * IOCSR addresses, and the send register targets another CPU by arch id. */
#define HALP_LA64_IPI_BASE              0x1000UL
#define HALP_LA64_IPI_STATUS            0x1000UL  /* own core, RO */
#define HALP_LA64_IPI_ENABLE            0x1004UL  /* own core, RW */
#define HALP_LA64_IPI_SET               0x1008UL
#define HALP_LA64_IPI_CLEAR             0x100CUL
#define HALP_LA64_IPI_SEND              0x1040UL  /* target<<16 | vector */
#define HALP_LA64_IPI_TARGET_SHIFT      16
#define HALP_LA64_IPI_TARGET_MASK       0x000003FFUL
#define HALP_LA64_IPI_VECTOR_MASK       0x0000001FUL
#define HALP_LA64_IPI_VECTOR            0          /* the only vector used */

/* PCH-PIC (7A1000 bridge PIC), MMIO. Register offsets are 7A1000 user manual
 * chapter 5.2 Table 65 and Linux's irq-loongson-pch-pic.c; each 64-source
 * class is two 32-bit registers. */
#define HALP_LA64_PCH_PIC_ID            0x000   /* 0x07000000 in 31:24 */
#define HALP_LA64_PCH_PIC_MASK          0x020   /* 1 = masked */
#define HALP_LA64_PCH_PIC_HTMSI_EN      0x040
#define HALP_LA64_PCH_PIC_EDGE          0x060
#define HALP_LA64_PCH_PIC_CLR           0x080   /* write 1 clears edge */
#define HALP_LA64_PCH_PIC_AUTO0         0x0C0
#define HALP_LA64_PCH_PIC_AUTO1         0x0E0
#define HALP_LA64_PCH_PIC_ROUTE         0x100   /* 1 byte per source */
#define HALP_LA64_PCH_PIC_HTVEC         0x200
#define HALP_LA64_PCH_PIC_IRR           0x380
#define HALP_LA64_PCH_PIC_ISR           0x3A0
#define HALP_LA64_PCH_PIC_POL           0x3E0
#define HALP_LA64_PCH_PIC_SOURCES       64
#define HALP_LA64_PCH_PIC_ID_VALUE      0x07000000UL /* 7A1000 Table 66 id */

/* PCH-MSI: a write of the vector in bits 7:0 to this window raises EIOINTC
 * input <vector>; QEMU loongarch_pch_msi.c, 8-byte window. */
#define HALP_LA64_PCH_MSI_VECTOR_MASK   0x000000FFUL

/* LIOINTC, MMIO. Only the 7A/3A boards have it; QEMU's virt machine creates
 * no LIOINTC device (hw/loongarch/virt.c virt_irq_init), so the HAL takes its
 * base from a platform description and leaves it unused when absent.
 * Offsets: drivers/irqchip/irq-loongson-liointc.c. */
#define HALP_LA64_LIOINTC_MAP           0x000   /* 1 byte per source */
#define HALP_LA64_LIOINTC_STATUS(core)  (0x040 + (core) * 8)
#define HALP_LA64_LIOINTC_EN_STATUS     0x024
#define HALP_LA64_LIOINTC_ENABLE        0x028
#define HALP_LA64_LIOINTC_DISABLE       0x02C
#define HALP_LA64_LIOINTC_POL           0x030
#define HALP_LA64_LIOINTC_EDGE          0x034
#define HALP_LA64_LIOINTC_SOURCES       32
#define HALP_LA64_LIOINTC_CORES         4
/* Vector space: EIOINTC sources keep their hardware number 0..255, and the
 * LIOINTC sources of a board that has one follow at 256..287. */
#define HALP_LA64_LIOINTC_VECTOR_BASE   256UL

/* ---------------------------------------------------------------------------
 * PCIe
 *
 * One ECAM window, one MMIO window and one MMIO-mapped I/O window. Addresses
 * are the QEMU LoongArch virt board's (include/hw/loongarch/virt.h
 * VIRT_PCI_CFG_BASE/SIZE, VIRT_PCI_MEM_BASE/SIZE, VIRT_PCI_IO_BASE/SIZE).
 * ------------------------------------------------------------------------ */

#define HALP_LA64_PCI_CONFIG_SIZE       0x1000UL    /* per function */
#define HALP_LA64_PCI_BUS_SIZE          0x100000UL  /* per bus */

/* ---------------------------------------------------------------------------
 * Platform description
 *
 * The loader block carries no device description on this port: the LoongArch
 * loader block has no FDT field and its EarlyDeviceRanges/EarlyConsole fields
 * are written as zero by bootloader/arch/loongarch64/winldr.c. The HAL
 * therefore has exactly one non-ACPI source of board facts: a fixed table for
 * the machine it was built for, verified at runtime by reading the PCH-PIC
 * identification register. There is no ACPI parser in this HAL; a board that
 * is not the built-in one cannot be brought up until one exists.
 * ------------------------------------------------------------------------ */

#define HALP_LA64_PLATFORM_NONE         0
#define HALP_LA64_PLATFORM_QEMU_VIRT    1

typedef struct _HALP_LA64_PLATFORM
{
    ULONG Kind;

    /* Bridge interrupt controllers, all MMIO. Zero length = absent. */
    ULONG64 PchPicBase;
    ULONG PchPicLength;
    ULONG64 PchMsiBase;
    ULONG PchMsiLength;

    /* Local I/O interrupt controller, MMIO. Absent on QEMU virt. */
    ULONG64 LioIntcBase;
    ULONG LioIntcLength;

    /* PCIe host bridge. */
    ULONG64 PciEcamBase;
    ULONG PciEcamLength;
    ULONG64 PciIoBase;
    ULONG PciIoLength;
    ULONG64 PciMemoryBase;
    ULONG PciMemoryLength;
    ULONG PciFirstBus;
    ULONG PciLastBus;
    ULONG PciSegment;
    /* First EIOINTC/PCH-PIC source used by PCIe INTA; INTA..INTD are
     * consecutive. */
    ULONG PciIntxBase;

    /* 16550-compatible UARTs, byte registers, no shift. */
    ULONG64 UartBase;
    ULONG UartLength;
    ULONG UartCount;
    ULONG UartFirstIrq;

    /* 7A RTC (TOY), 32-bit registers. */
    ULONG64 RtcBase;
    ULONG RtcLength;
    ULONG RtcIrq;

    /* CPUCFG3.CCDMA of the boot CPU: hardware maintains DMA cache coherency. */
    BOOLEAN DmaCoherent;
} HALP_LA64_PLATFORM, *PHALP_LA64_PLATFORM;

/* ---------------------------------------------------------------------------
 * DMA
 *
 * Logical (bus) addresses are CPU physical addresses on this platform. The
 * three DMW windows [V1-7.5.18] give the CPU a cached (CC), uncached (SUC)
 * and write-combining (WUC) view of the same physical memory; they are VA
 * windows, not bus translations, so they do not change a DMA address. A
 * topology carries the windows only for platforms whose host bridge applies
 * an offset.
 * ------------------------------------------------------------------------ */

#define HALP_LA64_DMA_MAX_WINDOWS 4
#define HALP_LA64_DMA_TAG 'aDvL'

typedef struct _HALP_LA64_DMA_TOPOLOGY
{
    BOOLEAN Coherent;
    ULONG WindowCount;
    ULONG64 CpuBase[HALP_LA64_DMA_MAX_WINDOWS];
    ULONG64 BusBase[HALP_LA64_DMA_MAX_WINDOWS];
    ULONG64 Size[HALP_LA64_DMA_MAX_WINDOWS];
} HALP_LA64_DMA_TOPOLOGY;

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------ */

extern volatile ULONG HalpLa64InitializationPhase;
extern volatile ULONG HalpLa64InitializationFailure;
extern ULONG64 HalpLa64TimerFrequency;
extern ULONG64 HalpLa64BootCpuId;
extern ULONG64 HalpLa64CpuCfg1;
extern ULONG64 HalpLa64CpuCfg2;
extern ULONG64 HalpLa64CpuCfg3;
extern ULONG HalpLa64CurrentTimeIncrement;
extern ULONG HalpLa64ProfileInterval;
extern HALP_LA64_PLATFORM HalpLa64Platform;
extern ULONG64 HalpLa64PhysicalLimit;
extern ULONG64 HalpLa64DirectMapBase;
extern ULONG64 HalpLa64UncachedBase;
extern ULONG64 HalpLa64WriteCombineBase;

/* ---------------------------------------------------------------------------
 * Bootstrap services
 * ------------------------------------------------------------------------ */

BOOLEAN HalpLa64AdoptLoaderBlock(_In_ struct _LOADER_PARAMETER_BLOCK *LoaderBlock);

VOID HalpLa64StartClock(VOID);
VOID NTAPI HalpLa64ClockInterrupt(_In_ PKTRAP_FRAME TrapFrame);
VOID NTAPI HalpLa64SuspendClockTick(VOID);
ULONG NTAPI HalpLa64ResumeClockTick(VOID);
ULONG64 HalpLa64ReadTime(VOID);
BOOLEAN HalpLa64ReadTimerFrequency(_Out_ PULONG64 Frequency);

ULONG_PTR NTAPI HalSetProfileInterval(_In_ ULONG_PTR Interval);
VOID HalpLa64RegisterProfileInformation(VOID);

BOOLEAN HalpLa64SelectPlatform(VOID);
BOOLEAN HalpLa64MapPlatform(VOID);
ULONG HalpLa64IrqlForVector(_In_ ULONG Vector);
BOOLEAN HalpLa64ValidVector(_In_ ULONG Vector);

BOOLEAN HalpLa64InitializeInterruptControllers(VOID);
BOOLEAN HalpLa64MapInterruptControllers(VOID);
VOID HalpLa64EnableControllerSource(_In_ ULONG Vector);
VOID HalpLa64DisableControllerSource(_In_ ULONG Vector);
ULONG NTAPI HalpLa64ClaimInterrupt(VOID);
VOID NTAPI HalpLa64CompleteInterrupt(_In_ ULONG Vector);
VOID NTAPI HalpLa64IpiSend(_In_ KAFFINITY Targets);
VOID NTAPI HalpLa64IpiAcknowledge(VOID);
VOID HalpLa64EnableLineInterrupts(VOID);

BOOLEAN HalpLa64InitializePci(VOID);
BOOLEAN HalpLa64MapPciConfig(VOID);
BOOLEAN HalpLa64GetPciResource(_Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource);
BOOLEAN NTAPI HalpLa64GetPciBusRange(_Out_ PULONG FirstBus, _Out_ PULONG LastBus);
BOOLEAN HalpLa64PciDmaCoherent(VOID);
BOOLEAN HalpLa64PciHostPresent(VOID);

VOID HalpLa64InitializeDma(_In_ struct _LOADER_PARAMETER_BLOCK *LoaderBlock);
PDMA_ADAPTER HalpLa64CreateDmaAdapter(_In_ PDEVICE_DESCRIPTION Description,
                                      _In_ const HALP_LA64_DMA_TOPOLOGY *Topology,
                                      _Out_opt_ PULONG NumberOfMapRegisters);
BOOLEAN HalpLa64GetPlatformDmaTopology(
    _In_opt_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ HALP_LA64_DMA_TOPOLOGY *Topology);

BOOLEAN HalpLa64InitializeRtc(VOID);
BOOLEAN HalpLa64MapRtc(VOID);

BOOLEAN HalpLa64InitializeUart(VOID);
BOOLEAN HalpLa64MapUart(VOID);
VOID HalpLa64UartWrite(_In_reads_(Length) const CHAR *String, _In_ SIZE_T Length);
BOOLEAN HalpLa64GetUartResource(_In_ ULONG Index,
                                _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Memory,
                                _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Interrupt);

VOID HalpLa64InitializeCache(VOID);
VOID HalpLa64FlushDmaPhysical(_In_ ULONG64 Physical, _In_ SIZE_T Length);
ULONG HalpLa64CacheLineSize(VOID);

NTSTATUS NTAPI HaliInitPnpDriver(VOID);

/* Kernel entry points the HAL calls. Declarations match the kernel exports
 * this port already publishes; nothing new is required from ntoskrnl.spec. */
VOID NTAPI KeSetTimeIncrement(_In_ ULONG MaximumIncrement,
                              _In_ ULONG MinimumIncrement);
VOID NTAPI KeUpdateSystemTime(_In_ PKTRAP_FRAME TrapFrame, _In_ ULONG Increment,
                              _In_ KIRQL Irql);
VOID NTAPI KeUpdateRunTime(_In_ PKTRAP_FRAME TrapFrame, _In_ KIRQL Irql);
VOID NTAPI KeProfileInterruptWithSource(_In_ PKTRAP_FRAME TrapFrame,
                                        _In_ KPROFILE_SOURCE Source);

/* Kernel bridge, exported from this HAL with -arch=loongarch64 entries in
 * hal/hal.spec. The names are fixed so the ke port can declare them. */
ULONG NTAPI HalpLa64QueryProcessorId(_In_ ULONG ProcessorNumber);
