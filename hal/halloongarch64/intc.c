/*
 * PROJECT:     LiberNT LoongArch64 HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Line interrupt ownership: EIOINTC, PCH-PIC, PCH-MSI, LIOINTC, IPI
 *
 * The core records thirteen line interrupts in ESTAT.IS [V1-6.1.1]; this file
 * owns everything behind the eight hardware lines (HWI0..HWI7, IS[9:2]). The
 * board controllers are not part of the architecture manual:
 *
 *   EIOINTC   Loongson's extended I/O controller, an IOCSR device with 256
 *             sources. Register map and the "32 sources per HWI pin" routing
 *             are those of Loongson's own drivers (irq-loongson-eiointc.c) and
 *             match QEMU's model (hw/intc/loongarch_extioi.c).
 *   PCH-PIC   the 7A1000 bridge interrupt controller, MMIO, 64 sources:
 *             7A1000 user manual chapter 5.2 Table 65.
 *   PCH-MSI   the bridge MSI receiver: a write of the vector in bits 7:0 to
 *             the window raises the matching EIOINTC source (7A1000 chapter 22,
 *             QEMU loongarch_pch_msi.c).
 *   LIOINTC   the SoC-local controller of 3A/7A boards; absent from QEMU's
 *             virt machine, so it is only used when a platform description
 *             names it. Offsets: irq-loongson-liointc.c.
 *
 * Vector numbering. The HAL publishes the hardware source number as the NT
 * vector, so a driver sees the same number it would in a resource descriptor:
 *   0..255    EIOINTC source (PCH-PIC pins 0..31, PCH-MSI vectors 32..255)
 *   256..287  LIOINTC source, when that controller exists
 * The 64-based ACPI GSI numbering Linux uses does not exist here because there
 * is no ACPI table to define it.
 */

#include <ntifs.h>
#include <ioaccess.h>
#include <ndk/halfuncs.h>
#include "halp.h"

#define HALP_LA64_INVALID_VECTOR      MAXULONG
/* Secondary (GPIO-style) interrupt controllers own 512 vectors above the
 * platform's own 0..287 range; 288..511 stay unused so a future controller
 * with more sources has room. */
#define HALP_LA64_SECONDARY_BASE      512UL

static PUCHAR HalpLa64PchPic;
static volatile ULONG *HalpLa64LioIntc;
static KSPIN_LOCK HalpLa64PchLock;
static BOOLEAN HalpLa64EioIntcReady;
static BOOLEAN HalpLa64LioIntcReady;

static
ULONG
HalpLa64IocsrRead32(
    _In_ ULONG Address)
{
    ULONG Value;

    /* IOCSRRD.W rd, rj: rj holds the 32-bit IOCSR address [V1-4.2.2.1]. */
    __asm__ __volatile__("iocsrrd.w %0, %1" : "=r"(Value) : "r"(Address));
    return Value;
}

static
VOID
HalpLa64IocsrWrite32(
    _In_ ULONG Address,
    _In_ ULONG Value)
{
    __asm__ __volatile__("iocsrwr.w %0, %1"
                         :
                         : "r"(Value), "r"(Address)
                         : "memory");
}

static
VOID
HalpLa64PchWrite(
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(HalpLa64PchPic + Offset), Value);
}

static
ULONG
HalpLa64PchRead(
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(HalpLa64PchPic + Offset));
}

/* ---------------------------------------------------------------------------
 * Controller programming
 * ------------------------------------------------------------------------ */

BOOLEAN
HalpLa64InitializeInterruptControllers(VOID)
{
    ULONG Group, Word, Pin;

    if (HalpLa64Platform.PchPicBase == 0 && HalpLa64Platform.LioIntcBase == 0)
        return FALSE;

    /* Route each group of 32 EIOINTC sources to the HWI pin of the same
     * index. That is what makes the source number fix the IRQL in
     * HalpLa64IrqlForVector and keeps [V1-6.1.2] ordering. A byte is a
     * one-hot pin selector (irq-loongson-eiointc.c eiointc_router_init). */
    for (Word = 0; Word < HALP_LA64_EIOINTC_GROUPS / 4; ++Word)
    {
        ULONG Data = 0;

        for (Pin = 0; Pin < 4; ++Pin)
        {
            Group = Word * 4 + Pin;
            Data |= (1u << Group) << (Pin * 8);
        }
        HalpLa64IocsrWrite32(HALP_LA64_EIOINTC_IPMAP + Word * 4, Data);
    }

    /* Every source belongs to CPU 0. With the controller's CPU encoding off,
     * a route byte is a one-hot CPU selector, so 0x01 is "core 0". */
    for (Word = 0; Word < HALP_LA64_EIOINTC_SOURCES / 4; ++Word)
        HalpLa64IocsrWrite32(HALP_LA64_EIOINTC_ROUTE + Word * 4, 0x01010101UL);

    /* Enable all 256 sources in the controller. The per-device gate is the
     * PCH-PIC mask register, which starts fully masked and is opened by
     * HalEnableSystemInterrupt; MSI sources have no second gate. */
    for (Group = 0; Group < HALP_LA64_EIOINTC_GROUPS; ++Group)
        HalpLa64IocsrWrite32(HALP_LA64_EIOINTC_ENABLE + Group * 4, 0xFFFFFFFFUL);

    /* Retire anything the firmware left pending in this core's in-service
     * register. A stale bit would hold an HWI line asserted with no device
     * behind it, and the first claim would then return a vector no driver
     * owns. */
    for (Group = 0; Group < HALP_LA64_EIOINTC_GROUPS; ++Group)
        HalpLa64IocsrWrite32(HALP_LA64_EIOINTC_COREISR + Group * 4, 0xFFFFFFFFUL);

    /* TODO: the EIOINTC "node map" (IOCSR 0x14A0) selects the extended-IP
     * node type per 32-source group. Loongson's driver programs it for the
     * multi-node case but QEMU ignores it and neither the 7A1000 manual nor
     * Vol1 defines the field, so it is left at reset. */

    HalpLa64EioIntcReady = TRUE;
    return TRUE;
}

BOOLEAN
HalpLa64MapPlatform(VOID)
{
    PHYSICAL_ADDRESS Address;

    if (HalpLa64Platform.PchPicBase)
    {
        if (HalpLa64Platform.PchPicLength < 0x400)
            return FALSE;
        Address.QuadPart = (LONGLONG)HalpLa64Platform.PchPicBase;
        HalpLa64PchPic = MmMapIoSpace(Address, HalpLa64Platform.PchPicLength, MmNonCached);
        if (!HalpLa64PchPic)
            return FALSE;

        /* Prove the bridge is the device the platform description claims.
         * 7A1000 Table 66: INT_ID bits 31:24 hold the controller id 0x07.
         * There is no ACPI namespace to trust here, so this read is the only
         * evidence the fixed table applies to this machine. */
        if ((HalpLa64PchRead(HALP_LA64_PCH_PIC_ID) & 0xFF000000UL) !=
            HALP_LA64_PCH_PIC_ID_VALUE)
        {
            static const CHAR Message[] =
                "\r\nHAL: this machine is not the LoongArch64 QEMU virt platform "
                "this HAL was built for (7A1000 PCH-PIC INT_ID mismatch).\r\n"
                "HAL: no ACPI table is present and this HAL has no ACPI path, so "
                "it cannot describe or boot this machine.\r\n";
            const SIZE_T Length = sizeof(Message) - 1;

            DbgPrint("%.*s", (int)Length, Message);
            HalpLa64UartWrite(Message, Length);
            return FALSE;
        }

        /* Everything masked, level triggered, fixed distribution. */
        KeInitializeSpinLock(&HalpLa64PchLock);
        HalpLa64PchWrite(HALP_LA64_PCH_PIC_MASK, 0xFFFFFFFFUL);
        HalpLa64PchWrite(HALP_LA64_PCH_PIC_MASK + 4, 0xFFFFFFFFUL);
        HalpLa64PchWrite(HALP_LA64_PCH_PIC_EDGE, 0);
        HalpLa64PchWrite(HALP_LA64_PCH_PIC_EDGE + 4, 0);
        HalpLa64PchWrite(HALP_LA64_PCH_PIC_POL, 0);
        HalpLa64PchWrite(HALP_LA64_PCH_PIC_POL + 4, 0);
        HalpLa64PchWrite(HALP_LA64_PCH_PIC_AUTO0, 0);
        HalpLa64PchWrite(HALP_LA64_PCH_PIC_AUTO1, 0);
    }

    if (HalpLa64Platform.LioIntcBase)
    {
        Address.QuadPart = (LONGLONG)HalpLa64Platform.LioIntcBase;
        HalpLa64LioIntc = MmMapIoSpace(Address, HalpLa64Platform.LioIntcLength,
                                       MmNonCached);
        if (!HalpLa64LioIntc)
            return FALSE;
        /* Level triggered, all sources masked (irq-loongson-liointc.c
         * liointc_init). */
        WRITE_REGISTER_ULONG((volatile ULONG *)((PUCHAR)HalpLa64LioIntc +
                                                HALP_LA64_LIOINTC_EDGE), 0);
        WRITE_REGISTER_ULONG((volatile ULONG *)((PUCHAR)HalpLa64LioIntc +
                                                HALP_LA64_LIOINTC_POL), 0);
        WRITE_REGISTER_ULONG((volatile ULONG *)((PUCHAR)HalpLa64LioIntc +
                                                HALP_LA64_LIOINTC_DISABLE),
                             0xFFFFFFFFUL);
        HalpLa64LioIntcReady = TRUE;
    }

    return TRUE;
}

BOOLEAN
HalpLa64MapInterruptControllers(VOID)
{
    /* The IPI receiver: accept vector 0 and clear any stale status left by the
     * firmware. Anything else in the status register would raise IS[12] with
     * no owner to clear it. */
    HalpLa64IocsrWrite32(HALP_LA64_IPI_ENABLE, 1u << HALP_LA64_IPI_VECTOR);
    HalpLa64IocsrWrite32(HALP_LA64_IPI_CLEAR, 0xFFFFFFFFUL);
    return TRUE;
}

/* ---------------------------------------------------------------------------
 * Secondary controller vector space
 *
 * halsecondary.h asks the architecture where its secondary-interrupt range
 * starts. The HAL defines that here: the platform's own sources use 0..287, so
 * a registered secondary controller's GSI and internal-vector numbering starts
 * at 512.
 * ------------------------------------------------------------------------ */

ULONG
HalpSecondaryArchGsivBase(VOID)
{
    return HALP_LA64_SECONDARY_BASE;
}

ULONG
HalpSecondaryArchVectorBase(VOID)
{
    return HALP_LA64_SECONDARY_BASE;
}

/* ---------------------------------------------------------------------------
 * Per-source enable/disable
 * ------------------------------------------------------------------------ */

VOID
HalpLa64EnableControllerSource(
    _In_ ULONG Vector)
{
    KIRQL OldIrql;

    if (Vector < HALP_LA64_EIOINTC_SOURCES && HalpLa64EioIntcReady)
    {
        ULONG Offset = HALP_LA64_EIOINTC_ENABLE + (Vector / 32) * 4;

        /* The enable register is a plain bitmap, so the bit is set in a
         * read-modify-write sequence; a blind store would clear the other 31
         * sources of the group. */
        HalpLa64IocsrWrite32(Offset, HalpLa64IocsrRead32(Offset) |
                                     (1u << (Vector % 32)));
        if ((Vector < 32) && HalpLa64PchPic)
        {
            /* PCH-PIC mask: 0 enables. The two 32-bit words cover 64 pins. */
            KeAcquireSpinLock(&HalpLa64PchLock, &OldIrql);
            HalpLa64PchWrite(HALP_LA64_PCH_PIC_MASK + (Vector / 32) * 4,
                             HalpLa64PchRead(HALP_LA64_PCH_PIC_MASK +
                                             (Vector / 32) * 4) &
                             ~(1u << (Vector % 32)));
            KeReleaseSpinLock(&HalpLa64PchLock, OldIrql);
        }
    }
    else if (Vector >= HALP_LA64_LIOINTC_VECTOR_BASE &&
             Vector < HALP_LA64_LIOINTC_VECTOR_BASE + HALP_LA64_LIOINTC_SOURCES &&
             HalpLa64LioIntcReady)
    {
        ULONG Source = Vector - HALP_LA64_LIOINTC_VECTOR_BASE;
        ULONG Parent = Source / (HALP_LA64_LIOINTC_SOURCES / HALP_LA64_LIOINTC_CORES);
        PUCHAR Base = (PUCHAR)HalpLa64LioIntc;

        /* Map byte: one-hot core in 3:0, parent INTn in 7:4
         * (irq-loongson-liointc.c map_cache construction). */
        WRITE_REGISTER_UCHAR(Base + HALP_LA64_LIOINTC_MAP + Source,
                             (UCHAR)((1u << Parent) << 4) | 0x01);
        WRITE_REGISTER_ULONG((volatile ULONG *)(Base + HALP_LA64_LIOINTC_ENABLE),
                             1u << Source);
    }
}

VOID
HalpLa64DisableControllerSource(
    _In_ ULONG Vector)
{
    KIRQL OldIrql;

    if (Vector < HALP_LA64_EIOINTC_SOURCES && HalpLa64EioIntcReady)
    {
        if ((Vector < 32) && HalpLa64PchPic)
        {
            KeAcquireSpinLock(&HalpLa64PchLock, &OldIrql);
            HalpLa64PchWrite(HALP_LA64_PCH_PIC_MASK + (Vector / 32) * 4,
                             HalpLa64PchRead(HALP_LA64_PCH_PIC_MASK +
                                             (Vector / 32) * 4) |
                             (1u << (Vector % 32)));
            KeReleaseSpinLock(&HalpLa64PchLock, OldIrql);
        }
        {
            ULONG Offset = HALP_LA64_EIOINTC_ENABLE + (Vector / 32) * 4;

            HalpLa64IocsrWrite32(Offset, HalpLa64IocsrRead32(Offset) &
                                         ~(1u << (Vector % 32)));
        }
    }
    else if (Vector >= HALP_LA64_LIOINTC_VECTOR_BASE &&
             Vector < HALP_LA64_LIOINTC_VECTOR_BASE + HALP_LA64_LIOINTC_SOURCES &&
             HalpLa64LioIntcReady)
    {
        ULONG Source = Vector - HALP_LA64_LIOINTC_VECTOR_BASE;

        WRITE_REGISTER_ULONG((volatile ULONG *)((PUCHAR)HalpLa64LioIntc +
                                                HALP_LA64_LIOINTC_DISABLE),
                             1u << Source);
    }
}

/* ---------------------------------------------------------------------------
 * Claim and complete
 * ------------------------------------------------------------------------ */

/* Called from the kernel's IS[9:2] dispatch with interrupts masked at
 * SYNCH_LEVEL. Returns the source number, or HALP_LA64_INVALID_VECTOR when no
 * source is pending (a shared-line notification from a source another CPU
 * already acknowledged). */
ULONG
NTAPI
HalpLa64ClaimInterrupt(VOID)
{
    ULONG Group, Pending, Bit;

    if (HalpLa64EioIntcReady)
    {
        for (Group = 0; Group < HALP_LA64_EIOINTC_GROUPS; ++Group)
        {
            Pending = HalpLa64IocsrRead32(HALP_LA64_EIOINTC_COREISR + Group * 4);
            if (!Pending)
                continue;
            Bit = 0;
            while (!(Pending & (1u << Bit)))
                ++Bit;
            return Group * 32 + Bit;
        }
    }

    if (HalpLa64LioIntcReady)
    {
        ULONG Core = (ULONG)(HalpLa64BootCpuId % HALP_LA64_LIOINTC_CORES);

        Pending = READ_REGISTER_ULONG((volatile ULONG *)((PUCHAR)HalpLa64LioIntc +
                                                         HALP_LA64_LIOINTC_STATUS(Core)));
        if (Pending)
        {
            Bit = 0;
            while (!(Pending & (1u << Bit)))
                ++Bit;
            return HALP_LA64_LIOINTC_VECTOR_BASE + Bit;
        }
    }
    return HALP_LA64_INVALID_VECTOR;
}

/* Write one to the source's bit in the per-core in-service register. That is
 * how EIOINTC drops the source and, with no other bit of the group pending,
 * lowers the HWI line (hw/intc/loongarch_extioi.c COREISR write path). */
VOID
NTAPI
HalpLa64CompleteInterrupt(
    _In_ ULONG Vector)
{
    if (Vector < HALP_LA64_EIOINTC_SOURCES && HalpLa64EioIntcReady)
    {
        HalpLa64IocsrWrite32(HALP_LA64_EIOINTC_COREISR + (Vector / 32) * 4,
                             1u << (Vector % 32));
        if ((Vector < HALP_LA64_PCH_PIC_SOURCES) && HalpLa64PchPic)
        {
            /* Edge-triggered PCH-PIC inputs latch in INTIRR and need the
             * write-one-to-clear register; for level inputs this write has no
             * effect and the line drops when the device acknowledges. */
            KIRQL OldIrql;

            KeAcquireSpinLock(&HalpLa64PchLock, &OldIrql);
            HalpLa64PchWrite(HALP_LA64_PCH_PIC_CLR + (Vector / 32) * 4,
                             1u << (Vector % 32));
            KeReleaseSpinLock(&HalpLa64PchLock, OldIrql);
        }
    }
    /* A LIOINTC source is level-triggered and has no clear register: the input
     * drops when the interrupting device acknowledges, and the controller has
     * no in-service state to retire (irq-loongson-liointc.c never writes
     * STATUS). */
}

/* ---------------------------------------------------------------------------
 * IPI
 * ------------------------------------------------------------------------ */

VOID
NTAPI
HalpLa64IpiSend(
    _In_ KAFFINITY Targets)
{
    ULONG Cpu;

    /* Processor index equals arch id (CSR.CPUID) on this platform: the loader
     * block publishes the boot core's CPUID and the machine numbers its cores
     * from zero. A platform whose CPUIDs are sparse needs the ACPI MADT
     * mapping that this HAL does not implement. */
    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS; ++Cpu)
    {
        ULONG64 Value;

        if (!(Targets & ((KAFFINITY)1 << Cpu)))
            continue;
        Value = ((ULONG64)Cpu << HALP_LA64_IPI_TARGET_SHIFT) |
                (HALP_LA64_IPI_VECTOR & HALP_LA64_IPI_VECTOR_MASK);
        HalpLa64IocsrWrite32(HALP_LA64_IPI_SEND, (ULONG)Value);
    }
}

VOID
NTAPI
HalpLa64IpiAcknowledge(VOID)
{
    /* The status register is per-core and reached at its own address; writing
     * one to a status bit clears it and drops IS[12]. */
    HalpLa64IocsrWrite32(HALP_LA64_IPI_CLEAR, 1u << HALP_LA64_IPI_VECTOR);
}

VOID
NTAPI
HalRequestIpi(
    _In_ KAFFINITY Targets)
{
    HalpLa64IpiSend(Targets);
}

/* ---------------------------------------------------------------------------
 * Line-interrupt HAL entry points
 * ------------------------------------------------------------------------ */

BOOLEAN
NTAPI
HalEnableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE InterruptMode)
{
    UNREFERENCED_PARAMETER(Irql);
    UNREFERENCED_PARAMETER(InterruptMode);
    if (!HalpLa64ValidVector(Vector))
        return FALSE;
    HalpLa64EnableControllerSource(Vector);
    return TRUE;
}

VOID
NTAPI
HalDisableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql)
{
    UNREFERENCED_PARAMETER(Irql);
    if (HalpLa64ValidVector(Vector))
        HalpLa64DisableControllerSource(Vector);
}

BOOLEAN
NTAPI
HalBeginSystemInterrupt(
    _In_ KIRQL Irql,
    _In_ ULONG Vector,
    _Out_ PKIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(Vector);

    if (Irql <= KeGetCurrentIrql())
        return FALSE;
    *OldIrql = KfRaiseIrql(Irql);
    return TRUE;
}

VOID
NTAPI
HalEndSystemInterrupt(
    _In_ KIRQL OldIrql,
    _In_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    KfLowerIrql(OldIrql);
}

ULONG
NTAPI
HalGetInterruptVector(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG BusInterruptLevel,
    _In_ ULONG BusInterruptVector,
    _Out_ PKIRQL Irql,
    _Out_ PKAFFINITY Affinity)
{
    ULONG FirstBus, LastBus, Vector;

    if (Irql && Affinity && InterfaceType == Internal)
    {
        Vector = HalpSecondaryTranslateGsi(BusInterruptLevel);
        if (Vector != (ULONG)-1)
        {
            *Irql = HalpSecondaryIrql(Vector);
            *Affinity = 1;
            return Vector;
        }
    }
    if (!Irql || !Affinity || BusInterruptLevel != BusInterruptVector)
        return 0;
    if (InterfaceType == PCIBus)
    {
        if (!HalpLa64GetPciBusRange(&FirstBus, &LastBus) ||
            BusNumber < FirstBus || BusNumber > LastBus)
            return 0;
    }
    else if (InterfaceType != Internal)
    {
        return 0;
    }
    if (!HalpLa64ValidVector(BusInterruptLevel))
        return 0;

    Vector = BusInterruptLevel;
    *Irql = HalpLa64IrqlForVector(Vector);
    *Affinity = 1; /* Every external source is routed to core 0. */
    return Vector;
}

/* The software-interrupt lines are the two ESTAT.IS bits the core marks RW
 * [V1-7.4.6]; every other IS bit is read-only. SWI0 carries APC delivery and
 * SWI1 carries DISPATCH delivery, so the kernel can still distinguish "APC
 * pending" from "DPC pending" from ESTAT alone. */
VOID
FASTCALL
HalRequestSoftwareInterrupt(
    _In_ KIRQL Irql)
{
    ULONG Bit = (Irql <= APC_LEVEL) ? HALP_LA64_IRQ_SWI0 : HALP_LA64_IRQ_SWI1;

    __asm__ __volatile__("csrxchg %0, %1, %2"
                         :
                         : "r"(1u << Bit), "r"(1u << Bit),
                           "i"(HALP_LA64_CSR_ESTAT)
                         : "memory");
}

VOID
FASTCALL
HalClearSoftwareInterrupt(
    _In_ KIRQL Irql)
{
    ULONG Bit = (Irql <= APC_LEVEL) ? HALP_LA64_IRQ_SWI0 : HALP_LA64_IRQ_SWI1;

    /* csrxchg with a zero value and a one-bit mask clears only that bit. */
    __asm__ __volatile__("csrxchg %0, %1, %2"
                         :
                         : "r"(0u), "r"(1u << Bit), "i"(HALP_LA64_CSR_ESTAT)
                         : "memory");
}

VOID
NTAPI
HalSendSoftwareInterrupt(
    _In_ KAFFINITY TargetSet,
    _In_ KIRQL Irql)
{
    /* ESTAT.IS[1:0] is core-local state with no cross-core write path.
     * TODO: remote software interrupts need a payload-carrying IPI (the IPI
     * mailbox buffer words at IOCSR 0x1020-0x1038) and a handler that turns
     * one into HalRequestSoftwareInterrupt on the target; this HAL starts no
     * secondary cores, so only the local request exists. */
    UNREFERENCED_PARAMETER(TargetSet);
    HalRequestSoftwareInterrupt(Irql);
}
