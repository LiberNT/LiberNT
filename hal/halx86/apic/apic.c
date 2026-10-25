/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GNU GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/apic/apic.c
 * PURPOSE:         HAL APIC Management and Control Code
 * PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
 * REFERENCES:      https://web.archive.org/web/20190407074221/http://www.joseflores.com/docs/ExploringIrql.html
 *                  https://www.codeproject.com/KB/system/soviet_kernel_hack.aspx
 *                  http://bbs.unixmap.net/thread-2022-1-1.html (DEAD_LINK)
 *                  https://codemachine.com/articles/interrupt_dispatching.html
 *                  https://www.osronline.com/article.cfm%5Earticle=211.htm
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include "apicp.h"
#include <smp.h>
#define NDEBUG
#include <debug.h>

#ifndef _M_AMD64
#define APIC_LAZY_IRQL
#endif

/* GLOBALS ********************************************************************/

ULONG ApicVersion;
UCHAR HalpVectorToIndex[256];
ULONG HalpIoApicMaxIrq = APIC_MAX_IRQ;
BOOLEAN HalpApicHvMsrs;

extern HALP_APIC_INFO_TABLE HalpApicInfoTable;

typedef struct _HALP_IOAPIC
{
    ULONG_PTR Base;
    ULONG GsiBase;
    ULONG PinCount;
    ULONG SavedId;
} HALP_IOAPIC, *PHALP_IOAPIC;

#define HALP_NO_IOAPIC 0xFF

static HALP_IOAPIC HalpIoApics[APIC_MAX_GSI_PINS];
static ULONG HalpIoApicCount;
static UCHAR HalpGsiToIoApic[APIC_MAX_GSI_PINS];

/* The shared IOREGSEL/IOWIN select/access pair must not interleave between
   CPUs, and level lines are masked from interrupt context: any-IRQL lock. */
static KSPIN_LOCK HalpIoApicPairLock;
static ULONG_PTR HalpIoApicPairFlags;

static
VOID
HalpAcquireIoApicPairLock(VOID)
{
    HalpAcquireRaisedSpinLock(&HalpIoApicPairLock, &HalpIoApicPairFlags);
}

static
VOID
HalpReleaseIoApicPairLock(VOID)
{
    HalpReleaseRaisedSpinLock(&HalpIoApicPairLock, HalpIoApicPairFlags);
}

#ifndef _M_AMD64
const UCHAR
HalpIRQLtoTPR[32] =
{
    0x00, /*  0 PASSIVE_LEVEL */
    0x3d, /*  1 APC_LEVEL */
    0x41, /*  2 DISPATCH_LEVEL */
    0x41, /*  3 \  */
    0x51, /*  4  \ */
    0x61, /*  5  | */
    0x71, /*  6  | */
    0x81, /*  7  | */
    0x91, /*  8  | */
    0xa1, /*  9  | */
    0xb1, /* 10  | */
    0xb1, /* 11  | */
    0xb1, /* 12  | */
    0xb1, /* 13  | */
    0xb1, /* 14  | */
    0xb1, /* 15 DEVICE IRQL */
    0xb1, /* 16  | */
    0xb1, /* 17  | */
    0xb1, /* 18  | */
    0xb1, /* 19  | */
    0xb1, /* 20  | */
    0xb1, /* 21  | */
    0xb1, /* 22  | */
    0xb1, /* 23  | */
    0xb1, /* 24  | */
    0xb1, /* 25  / */
    0xb1, /* 26 /  */
    0xc1, /* 27 PROFILE_LEVEL */
    0xd1, /* 28 CLOCK2_LEVEL */
    0xe1, /* 29 IPI_LEVEL */
    0xef, /* 30 POWER_LEVEL */
    0xff, /* 31 HIGH_LEVEL */
};

const KIRQL
HalVectorToIRQL[16] =
{
       0, /* 00 PASSIVE_LEVEL */
    0xff, /* 10 */
    0xff, /* 20 */
       1, /* 3D APC_LEVEL */
       2, /* 41 DISPATCH_LEVEL */
       4, /* 50 \ */
       5, /* 60  \ */
       6, /* 70  | */
       7, /* 80 DEVICE IRQL */
       8, /* 90  | */
       9, /* A0  / */
      10, /* B0 /  */
      27, /* C1 PROFILE_LEVEL */
      28, /* D1 CLOCK2_LEVEL */
      29, /* E1 IPI_LEVEL / EF POWER_LEVEL */
      31, /* FF HIGH_LEVEL */
};
#endif

/* PRIVATE FUNCTIONS **********************************************************/

/* Raw select/access pairs — caller must hold the pair lock */
FORCEINLINE
ULONG
IOApicReadRaw(ULONG_PTR Base, UCHAR Register)
{
    WRITE_REGISTER_ULONG((PULONG)(Base + IOAPIC_IOREGSEL), Register);
    return READ_REGISTER_ULONG((PULONG)(Base + IOAPIC_IOWIN));
}

FORCEINLINE
VOID
IOApicWriteRaw(ULONG_PTR Base, UCHAR Register, ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Base + IOAPIC_IOREGSEL), Register);
    WRITE_REGISTER_ULONG((PULONG)(Base + IOAPIC_IOWIN), Value);
}

static __inline
ULONG
IOApicRead(ULONG_PTR Base, UCHAR Register)
{
    ULONG Value;

    HalpAcquireIoApicPairLock();
    Value = IOApicReadRaw(Base, Register);
    HalpReleaseIoApicPairLock();
    return Value;
}

static __inline
VOID
IOApicWrite(ULONG_PTR Base, UCHAR Register, ULONG Value)
{
    HalpAcquireIoApicPairLock();
    IOApicWriteRaw(Base, Register, Value);
    HalpReleaseIoApicPairLock();
}

static
PHALP_IOAPIC
HalpIoApicFromGsi(
    _In_ ULONG Gsi,
    _Out_ PUCHAR Pin)
{
    PHALP_IOAPIC IoApic;

    if (Gsi >= HalpIoApicMaxIrq || HalpGsiToIoApic[Gsi] == HALP_NO_IOAPIC)
        return NULL;

    IoApic = &HalpIoApics[HalpGsiToIoApic[Gsi]];
    *Pin = (UCHAR)(Gsi - IoApic->GsiBase);
    return IoApic;
}

/* One lock hold covers both dwords, keeping the 64-bit entry access atomic */
static __inline
VOID
ApicWriteIORedirectionEntry(
    UCHAR Index,
    IOAPIC_REDIRECTION_REGISTER ReDirReg)
{
    PHALP_IOAPIC IoApic;
    UCHAR Pin;

    ASSERT(Index < HalpIoApicMaxIrq);
    IoApic = HalpIoApicFromGsi(Index, &Pin);
    if (IoApic == NULL)
        return;

    HalpAcquireIoApicPairLock();
    IOApicWriteRaw(IoApic->Base, IOAPIC_REDTBL + 2 * Pin, ReDirReg.Long0);
    IOApicWriteRaw(IoApic->Base, IOAPIC_REDTBL + 2 * Pin + 1, ReDirReg.Long1);
    HalpReleaseIoApicPairLock();
}

static __inline
IOAPIC_REDIRECTION_REGISTER
ApicReadIORedirectionEntry(
    UCHAR Index)
{
    IOAPIC_REDIRECTION_REGISTER ReDirReg;
    PHALP_IOAPIC IoApic;
    UCHAR Pin;

    ASSERT(Index < HalpIoApicMaxIrq);
    IoApic = HalpIoApicFromGsi(Index, &Pin);
    if (IoApic == NULL)
    {
        ReDirReg.LongLong = 0;
        ReDirReg.Vector = APIC_FREE_VECTOR;
        ReDirReg.Mask = 1;
        return ReDirReg;
    }

    HalpAcquireIoApicPairLock();
    ReDirReg.Long0 = IOApicReadRaw(IoApic->Base, IOAPIC_REDTBL + 2 * Pin);
    ReDirReg.Long1 = IOApicReadRaw(IoApic->Base, IOAPIC_REDTBL + 2 * Pin + 1);
    HalpReleaseIoApicPairLock();

    return ReDirReg;
}

FORCEINLINE
VOID
ApicRequestSelfInterrupt(IN UCHAR Vector, UCHAR TriggerMode)
{
    ULONG Flags;
    APIC_INTERRUPT_COMMAND_REGISTER Icr;
    APIC_INTERRUPT_COMMAND_REGISTER IcrStatus;

    /*
     * The IRR registers are spaced 16 bytes apart and hold 32 status bits each.
     * Pre-compute the register and bit that match our vector.
     */
    ULONG VectorHigh = Vector / 32;
    ULONG VectorLow = Vector % 32;
    ULONG Irr = APIC_IRR + 0x10 * VectorHigh;
    ULONG IrrBit = 1UL << VectorLow;

    /* Setup the command register */
    Icr.LongLong = 0;
    Icr.Vector = Vector;
    Icr.MessageType = APIC_MT_Fixed;
    Icr.TriggerMode = TriggerMode;
    Icr.DestinationShortHand = APIC_DSH_Self;

    if (HalpApicHvMsrs && TriggerMode == APIC_TGM_Edge)
    {
        __writemsr(HV_X64_MSR_ICR, Icr.LongLong);
        return;
    }

    /* Disable interrupts so that we can change IRR without being interrupted */
    Flags = __readeflags();
    _disable();

    /* Wait for the APIC to be idle */
    do
    {
        IcrStatus.Long0 = ApicRead(APIC_ICR0);
    } while (IcrStatus.DeliveryStatus);

    /* Write high dword first, then low dword to send the interrupt */
    ApicWrite(APIC_ICR1, Icr.Long1);
    ApicWrite(APIC_ICR0, Icr.Long0);

    /* Wait until we see the interrupt request.
     * It will stay in requested state until we re-enable interrupts.
     */
    while (!(ApicRead(Irr) & IrrBit))
    {
        YieldProcessor();
    }

    /* Finally, restore the original interrupt state */
    if (Flags & EFLAGS_INTERRUPT_MASK)
    {
        _enable();
    }
}

VOID
HalpRequestSelfInterrupt(
    _In_ UCHAR Vector)
{
    ApicRequestSelfInterrupt(Vector, APIC_TGM_Edge);
}

FORCEINLINE
VOID
ApicSendEOI(void)
{
    if (HalpApicHvMsrs)
        __writemsr(HV_X64_MSR_EOI, 0);
    else
        ApicWrite(APIC_EOI, 0);
}

FORCEINLINE
KIRQL
ApicGetProcessorIrql(VOID)
{
    /* Read the TPR and convert it to an IRQL */
    return TprToIrql(ApicRead(APIC_PPR));
}

FORCEINLINE
KIRQL
ApicGetCurrentIrql(VOID)
{
#ifdef _M_AMD64
    return (KIRQL)__readcr8();
#elif defined(APIC_LAZY_IRQL)
    /* Return the field in the PCR */
    return (KIRQL)__readfsbyte(FIELD_OFFSET(KPCR, Irql));
#else
    /* Read the TPR and convert it to an IRQL */
    return TprToIrql(ApicRead(APIC_TPR));
#endif
}

FORCEINLINE
VOID
ApicSetIrql(KIRQL Irql)
{
#ifdef _M_AMD64
    __writecr8(Irql);
#elif defined(APIC_LAZY_IRQL)
    __writefsbyte(FIELD_OFFSET(KPCR, Irql), Irql);
#else
    /* Convert IRQL and write the TPR */
    ApicWrite(APIC_TPR, IrqlToTpr(Irql));
#endif
}
#define ApicRaiseIrql ApicSetIrql

#ifdef APIC_LAZY_IRQL
FORCEINLINE
VOID
ApicLowerIrql(KIRQL Irql)
{
    __writefsbyte(FIELD_OFFSET(KPCR, Irql), Irql);

    /* Is the new Irql lower than set in the TPR? */
    if (Irql < KeGetPcr()->IRR)
    {
        /* Save the new hard IRQL in the IRR field */
        KeGetPcr()->IRR = Irql;

        /* Need to lower it back */
        ApicWrite(APIC_TPR, IrqlToTpr(Irql));
    }
}
#else
#define ApicLowerIrql ApicSetIrql
#endif

UCHAR
FASTCALL
HalpIrqToVector(UCHAR Irq)
{
    IOAPIC_REDIRECTION_REGISTER ReDirReg;

    if (Irq >= HalpIoApicMaxIrq)
        return APIC_FREE_VECTOR;

    ReDirReg = ApicReadIORedirectionEntry(Irq);

    /* Return the vector */
    return (UCHAR)ReDirReg.Vector;
}

KIRQL
FASTCALL
HalpVectorToIrql(UCHAR Vector)
{
    return TprToIrql(Vector);
}

UCHAR
FASTCALL
HalpVectorToIrq(UCHAR Vector)
{
    return HalpVectorToIndex[Vector];
}

VOID
NTAPI
HalpSendEOI(VOID)
{
    ApicSendEOI();
}

static
BOOLEAN
ApicHvMsrsRecommended(VOID)
{
    INT CpuInfo[4];

    __cpuid(CpuInfo, 1);
    if (!(CpuInfo[2] & 0x80000000))
        return FALSE;

    __cpuid(CpuInfo, 0x40000000);
    if ((ULONG)CpuInfo[0] < 0x40000004)
        return FALSE;

    __cpuid(CpuInfo, 0x40000001);
    if ((ULONG)CpuInfo[0] != HV_CPUID_INTERFACE_HV1)
        return FALSE;

    __cpuid(CpuInfo, 0x40000003);
    if (!(CpuInfo[0] & HV_ACCESS_INTR_CTRL_REGS))
        return FALSE;

    __cpuid(CpuInfo, 0x40000004);
    return (CpuInfo[0] & (HV_APIC_ACCESS_RECOMMENDED | HV_X2APIC_MSRS_RECOMMENDED)) != 0;
}

VOID
NTAPI
ApicInitializeLocalApic(ULONG Cpu)
{
    APIC_BASE_ADDRESS_REGISTER BaseRegister;
    APIC_SPURIOUS_INERRUPT_REGISTER SpIntRegister;
    LVT_REGISTER LvtEntry;

    if (Cpu == 0)
    {
        HalpApicHvMsrs = ApicHvMsrsRecommended();
        DPRINT1("APIC: Hyper-V synthetic EOI/ICR MSRs %s\n", HalpApicHvMsrs ? "enabled" : "not used");
    }

    /* Enable the APIC if it wasn't yet */
    BaseRegister.LongLong = __readmsr(MSR_APIC_BASE);

    /* This HAL drives the LAPIC through the xAPIC MMIO window, which is
     * disabled while x2APIC EXTD is set; the only legal exit from x2APIC
     * is through the disabled state. */
    if (BaseRegister.ExtendedMode)
    {
        DPRINT1("APIC: CPU %lu handed over in x2APIC mode, forcing xAPIC\n", Cpu);
        BaseRegister.Enable = 0;
        BaseRegister.ExtendedMode = 0;
        __writemsr(MSR_APIC_BASE, BaseRegister.LongLong);
    }

    BaseRegister.Enable = 1;
    BaseRegister.BootStrapCPUCore = (Cpu == 0);
    __writemsr(MSR_APIC_BASE, BaseRegister.LongLong);

    /* Set spurious vector and SoftwareEnable to 1 */
    SpIntRegister.Long = ApicRead(APIC_SIVR);
    SpIntRegister.Vector = APIC_SPURIOUS_VECTOR;
    SpIntRegister.SoftwareEnable = 1;
    SpIntRegister.FocusCPUCoreChecking = 0;
    ApicWrite(APIC_SIVR, SpIntRegister.Long);

    /* Read the version and save it globally */
    if (Cpu == 0) ApicVersion = ApicRead(APIC_VER);

    /* Set the mode to flat (max 8 CPUs supported!) */
    ApicWrite(APIC_DFR, APIC_DF_Flat);

    /* Set logical apic ID */
    ApicWrite(APIC_LDR, ApicLogicalId(Cpu) << 24);

    /* Set the spurious ISR */
    KeRegisterInterruptHandler(APIC_SPURIOUS_VECTOR, ApicSpuriousService);

    /* Create a template LVT */
    LvtEntry.Long = 0;
    LvtEntry.Vector = APIC_FREE_VECTOR;
    LvtEntry.MessageType = APIC_MT_Fixed;
    LvtEntry.DeliveryStatus = 0;
    LvtEntry.RemoteIRR = 0;
    LvtEntry.TriggerMode = APIC_TGM_Edge;
    LvtEntry.Mask = 1;
    LvtEntry.TimerMode = 0;

    /* Initialize and mask LVTs */
    ApicWrite(APIC_TMRLVTR, LvtEntry.Long);
    ApicWrite(APIC_THRMLVTR, LvtEntry.Long);
    ApicWrite(APIC_PCLVTR, LvtEntry.Long);
    ApicWrite(APIC_EXT0LVTR, LvtEntry.Long);
    ApicWrite(APIC_EXT1LVTR, LvtEntry.Long);
    ApicWrite(APIC_EXT2LVTR, LvtEntry.Long);
    ApicWrite(APIC_EXT3LVTR, LvtEntry.Long);

    /* LINT0 */
    LvtEntry.Vector = APIC_SPURIOUS_VECTOR;
    LvtEntry.MessageType = APIC_MT_ExtInt;
    ApicWrite(APIC_LINT0, LvtEntry.Long);

    /* Enable LINT1 (NMI) */
    LvtEntry.Mask = 0;
    LvtEntry.Vector = APIC_NMI_VECTOR;
    LvtEntry.MessageType = APIC_MT_NMI;
    LvtEntry.TriggerMode = APIC_TGM_Level;
    ApicWrite(APIC_LINT1, LvtEntry.Long);

    /* Enable error LVTR */
    LvtEntry.Vector = APIC_ERROR_VECTOR;
    LvtEntry.MessageType = APIC_MT_Fixed;
    ApicWrite(APIC_ERRLVTR, LvtEntry.Long);

    /* Set the IRQL from the PCR */
    ApicSetIrql(KeGetPcr()->Irql);
#ifdef APIC_LAZY_IRQL
    /* Save the new hard IRQL in the IRR field */
    KeGetPcr()->IRR = KeGetPcr()->Irql;
#endif
}

UCHAR
NTAPI
HalpAllocateSystemInterrupt(
    _In_ UCHAR Irq,
    _In_ UCHAR Vector)
{
    IOAPIC_REDIRECTION_REGISTER ReDirReg;

    ASSERT(Irq < HalpIoApicMaxIrq);
    ASSERT(HalpVectorToIndex[Vector] == APIC_FREE_VECTOR);

    /* Setup a redirection entry. Use fixed physical delivery: lowest-priority logical delivery can land the vector on a CPU whose IDT was never patched by KeConnectInterrupt, where it would be swallowed by KiUnexpectedInterrupt. */
    ReDirReg.Vector = Vector;
    ReDirReg.MessageType = APIC_MT_Fixed;
    ReDirReg.DestinationMode = APIC_DM_Physical;
    ReDirReg.DeliveryStatus = 0;
    ReDirReg.Polarity = 0;
    ReDirReg.RemoteIRR = 0;
    ReDirReg.TriggerMode = APIC_TGM_Edge;
    ReDirReg.Mask = 1;
    ReDirReg.Reserved = 0;
    ReDirReg.Destination = ApicRead(APIC_ID) >> 24;

    /* Initialize entry */
    ApicWriteIORedirectionEntry(Irq, ReDirReg);

    /* Save irq in the table */
    HalpVectorToIndex[Vector] = Irq;

    return Vector;
}

ULONG
NTAPI
HalpGetRootInterruptVector(
    _In_ ULONG BusInterruptLevel,
    _In_ ULONG BusInterruptVector,
    _Out_ PKIRQL OutIrql,
    _Out_ PKAFFINITY OutAffinity)
{
    UCHAR Vector;

    if (HalpSecondaryIsGsiv(BusInterruptLevel))
        return HalpSecondaryGetRootVector(BusInterruptLevel, OutIrql, OutAffinity);

    /* Levels beyond the discovered pin count cannot be routed by a line;
     * reject so the caller falls back to MSI/deferred allocation. */
    if (BusInterruptLevel >= HalpIoApicMaxIrq || HalpGsiToIoApic[BusInterruptLevel] == HALP_NO_IOAPIC)
    {
        DPRINT1("HalpGetRootInterruptVector: out-of-range IRQ %lu (max %lu)\n", BusInterruptLevel, HalpIoApicMaxIrq);
        goto Reject;
    }

    /* If the IOAPIC redirection entry for this GSI is already
     * programmed, reuse the existing vector. */
    Vector = HalpIrqToVector(BusInterruptLevel);
    if (Vector != APIC_FREE_VECTOR)
    {
        NT_ASSERT(HalpVectorToIndex[Vector] == BusInterruptLevel);
        *OutIrql = HalpVectorToIrql(Vector);
    }
#ifdef _M_AMD64
    else if (BusInterruptLevel < APIC_MAX_IRQ)
    {
        /* Fixed mapping bounded to 0x30..0x47, disjoint from the MSI pool
         * (0x50..0xCF) by construction */
        Vector = (UCHAR)(PRIMARY_VECTOR_BASE + BusInterruptLevel);

        if (HalpVectorToIrq(Vector) != APIC_FREE_VECTOR)
        {
            DPRINT1("HalpGetRootInterruptVector: vector 0x%x already claimed for IRQ %lu\n",
                    Vector, BusInterruptLevel);
            goto Reject;
        }

        Vector = HalpAllocateSystemInterrupt(BusInterruptLevel, Vector);
        *OutIrql = HalpVectorToIrql(Vector);
    }
    else
    {
        /* GSIs past the fixed line window get a spillover vector below the
         * MSI pool; first-fit, never freed. */
        ULONG Candidate;

        Vector = 0;
        for (Candidate = APIC_SPILL_VECTOR_MIN; Candidate <= APIC_SPILL_VECTOR_MAX; Candidate++)
        {
            if (HalpVectorToIndex[Candidate] == APIC_FREE_VECTOR)
            {
                Vector = (UCHAR)Candidate;
                break;
            }
        }

        if (Vector == 0)
        {
            DPRINT1("HalpGetRootInterruptVector: spillover vector window exhausted for GSI %lu\n", BusInterruptLevel);
            goto Reject;
        }

        Vector = HalpAllocateSystemInterrupt((UCHAR)BusInterruptLevel, Vector);
        *OutIrql = HalpVectorToIrql(Vector);
    }
#else
    else
    {
        ULONG Candidate;

        /* x86 reserves priority classes 0x30 and 0x40 for APCs and DPCs.
         * Allocate from device priority classes, consulting the same vector
         * ownership table as MSI so neither allocator reuses a live vector. */
        for (Candidate = 0x50; Candidate < 0xC0; ++Candidate)
        {
            if (HalpVectorToIndex[Candidate] == APIC_FREE_VECTOR)
                break;
        }
        if (Candidate == 0xC0)
            goto Reject;

        Vector = HalpAllocateSystemInterrupt((UCHAR)BusInterruptLevel,
                                            (UCHAR)Candidate);
        *OutIrql = HalpVectorToIrql(Vector);
    }
#endif

    *OutAffinity = HalpDefaultInterruptAffinity;
    ASSERT(HalpDefaultInterruptAffinity);

    return Vector;

Reject:
    if (OutAffinity)
        *OutAffinity = 0;
    if (OutIrql)
        *OutIrql = 0;
    return 0;
}

static
BOOLEAN
HalpRegisterIoApic(
    _In_ ULONG_PTR Base,
    _In_ ULONG GsiBase,
    _In_ BOOLEAN Primary)
{
    PHALP_IOAPIC IoApic;
    ULONG VersionReg;
    ULONG PinCount;
    ULONG Gsi;

    if (GsiBase >= APIC_MAX_GSI_PINS)
        return FALSE;

    VersionReg = IOApicRead(Base, IOAPIC_VER);
    PinCount = ((VersionReg >> 16) & 0xFF) + 1;
    if (VersionReg == 0xFFFFFFFF || PinCount == 0x100 || PinCount == 0)
    {
        if (!Primary)
            return FALSE;
        PinCount = APIC_MAX_IRQ;
    }
    if (PinCount > APIC_MAX_GSI_PINS - GsiBase)
        PinCount = APIC_MAX_GSI_PINS - GsiBase;

    for (Gsi = GsiBase; Gsi < GsiBase + PinCount; Gsi++)
    {
        if (HalpGsiToIoApic[Gsi] != HALP_NO_IOAPIC)
            return FALSE;
    }

    IoApic = &HalpIoApics[HalpIoApicCount];
    IoApic->Base = Base;
    IoApic->GsiBase = GsiBase;
    IoApic->PinCount = PinCount;
    for (Gsi = GsiBase; Gsi < GsiBase + PinCount; Gsi++)
        HalpGsiToIoApic[Gsi] = (UCHAR)HalpIoApicCount;
    HalpIoApicCount++;

    if (GsiBase + PinCount > HalpIoApicMaxIrq)
        HalpIoApicMaxIrq = GsiBase + PinCount;

    DPRINT1("APIC: I/O APIC at %p, GSI %lu-%lu\n", (PVOID)Base, GsiBase, GsiBase + PinCount - 1);
    return TRUE;
}

VOID
NTAPI
ApicInitializeIOApic(VOID)
{
    PHARDWARE_PTE Pte;
    IOAPIC_REDIRECTION_REGISTER ReDirReg;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID Mapping;
    ULONG PrimaryAddress;
    ULONG Id;
    UCHAR Index;
    ULONG Vector;

    RtlFillMemory(HalpGsiToIoApic, sizeof(HalpGsiToIoApic), HALP_NO_IOAPIC);
    HalpIoApicCount = 0;
    HalpIoApicMaxIrq = 0;

    PrimaryAddress = IOAPIC_PHYS_BASE;
    for (Id = 0; Id < HALP_APIC_INFO_TABLE_IOAPIC_NUMBER; Id++)
    {
        if (HalpApicInfoTable.IoApicPA[Id] != 0 && HalpApicInfoTable.IoApicIrqBase[Id] == 0)
        {
            PrimaryAddress = HalpApicInfoTable.IoApicPA[Id];
            break;
        }
    }

    /* Map the I/O Apic page */
    Pte = HalAddressToPte(IOAPIC_BASE);
    Pte->PageFrameNumber = PrimaryAddress / PAGE_SIZE;
    Pte->Valid = 1;
    Pte->Write = 1;
    Pte->Owner = 1;
    Pte->CacheDisable = 1;
    Pte->Global = 1;
    _ReadWriteBarrier();

    HalpRegisterIoApic(IOAPIC_BASE + BYTE_OFFSET(PrimaryAddress), 0, TRUE);

    for (Id = 0; Id < HALP_APIC_INFO_TABLE_IOAPIC_NUMBER; Id++)
    {
        if (HalpApicInfoTable.IoApicPA[Id] == 0 ||
            HalpApicInfoTable.IoApicPA[Id] == PrimaryAddress ||
            HalpApicInfoTable.IoApicIrqBase[Id] >= APIC_MAX_GSI_PINS)
        {
            continue;
        }

        PhysicalAddress.QuadPart = HalpApicInfoTable.IoApicPA[Id];
        Mapping = HalpMapPhysicalMemory64(PhysicalAddress, 1);
        if (Mapping == NULL)
            continue;
        Pte = HalAddressToPte(Mapping);
        Pte->CacheDisable = 1;
        HalpFlushTLB();

        if (!HalpRegisterIoApic((ULONG_PTR)Mapping, HalpApicInfoTable.IoApicIrqBase[Id], FALSE))
            HalpUnmapVirtualAddress(Mapping, 1);
    }

    /* Setup a redirection entry */
    ReDirReg.Vector = APIC_FREE_VECTOR;
    ReDirReg.MessageType = APIC_MT_Fixed;
    ReDirReg.DestinationMode = APIC_DM_Physical;
    ReDirReg.DeliveryStatus = 0;
    ReDirReg.Polarity = 0;
    ReDirReg.RemoteIRR = 0;
    ReDirReg.TriggerMode = APIC_TGM_Edge;
    ReDirReg.Mask = 1;
    ReDirReg.Reserved = 0;
    ReDirReg.Destination = ApicRead(APIC_ID) >> 24;

    /* Loop all table entries */
    for (Index = 0; Index < HalpIoApicMaxIrq; Index++)
    {
        /* Initialize entry */
        ApicWriteIORedirectionEntry(Index, ReDirReg);
    }

    /* Init the vactor to index table */
    for (Vector = 0; Vector <= 255; Vector++)
    {
        HalpVectorToIndex[Vector] = APIC_FREE_VECTOR;
    }

    /* Enable the timer interrupt (but keep it masked) */
    ReDirReg.Vector = APIC_CLOCK_VECTOR;
    ReDirReg.MessageType = APIC_MT_Fixed;
    ReDirReg.DestinationMode = APIC_DM_Physical;
    ReDirReg.TriggerMode = APIC_TGM_Level;
    ReDirReg.Mask = 1;
    ReDirReg.Destination = ApicRead(APIC_ID) >> 24;
    ApicWriteIORedirectionEntry(APIC_CLOCK_INDEX, ReDirReg);
}

static IOAPIC_REDIRECTION_REGISTER HalpSavedIoApicEntries[APIC_MAX_GSI_PINS];

VOID
HalpSaveInterruptControllers(VOID)
{
    ULONG IoApic;
    UCHAR Index;

    for (IoApic = 0; IoApic < HalpIoApicCount; IoApic++)
        HalpIoApics[IoApic].SavedId = IOApicRead(HalpIoApics[IoApic].Base, IOAPIC_ID);
    for (Index = 0; Index < HalpIoApicMaxIrq; Index++)
        HalpSavedIoApicEntries[Index] = ApicReadIORedirectionEntry(Index);
}

ULONG
HalpSaveLocalInterrupts(VOID)
{
    return ApicRead(APIC_TPR);
}

VOID
HalpRestoreLocalInterrupts(
    _In_ ULONG Processor,
    _In_ ULONG Priority)
{
    ApicInitializeLocalApic(Processor);
    ApicWrite(APIC_TPR, Priority);
}

VOID
HalpRestoreInterruptControllers(VOID)
{
    IOAPIC_REDIRECTION_REGISTER ReDirReg;
    ULONG IoApic;
    UCHAR Index;

    ASSERT(!(__readeflags() & EFLAGS_INTERRUPT_MASK));

    HalpInitializeLegacyPICs();

    for (IoApic = 0; IoApic < HalpIoApicCount; IoApic++)
        IOApicWrite(HalpIoApics[IoApic].Base, IOAPIC_ID, HalpIoApics[IoApic].SavedId);
    for (Index = 0; Index < HalpIoApicMaxIrq; Index++)
    {
        ReDirReg = HalpSavedIoApicEntries[Index];
        ReDirReg.Mask = 1;
        ApicWriteIORedirectionEntry(Index, ReDirReg);
        if (!HalpSavedIoApicEntries[Index].Mask)
            ApicWriteIORedirectionEntry(Index, HalpSavedIoApicEntries[Index]);
    }
}

VOID
NTAPI
HalpInitializePICs(IN BOOLEAN EnableInterrupts)
{
    ULONG_PTR EFlags;

    /* Save EFlags and disable interrupts */
    EFlags = __readeflags();
    _disable();

    /* Initialize and mask the PIC */
    HalpInitializeLegacyPICs();

    /* Initialize the I/O APIC */
    ApicInitializeIOApic();

    /* Manually reserve some vectors */
    HalpVectorToIndex[APC_VECTOR] = APIC_RESERVED_VECTOR;
    HalpVectorToIndex[DISPATCH_VECTOR] = APIC_RESERVED_VECTOR;
    HalpVectorToIndex[APIC_CLOCK_VECTOR] = 8;
    HalpVectorToIndex[CLOCK_IPI_VECTOR] = APIC_RESERVED_VECTOR;
    HalpVectorToIndex[APIC_SPURIOUS_VECTOR] = APIC_RESERVED_VECTOR;

    /* Set interrupt handlers in the IDT */
    KeRegisterInterruptHandler(APIC_CLOCK_VECTOR, HalpClockInterrupt);
    KeRegisterInterruptHandler(CLOCK_IPI_VECTOR, HalpClockIpi);
#ifndef _M_AMD64
    KeRegisterInterruptHandler(APC_VECTOR, HalpApcInterrupt);
    KeRegisterInterruptHandler(DISPATCH_VECTOR, HalpDispatchInterrupt);
#endif

    /* Register the vectors for APC and dispatch interrupts */
    HalpRegisterVector(IDT_INTERNAL, 0, APC_VECTOR, APC_LEVEL);
    HalpRegisterVector(IDT_INTERNAL, 0, DISPATCH_VECTOR, DISPATCH_LEVEL);

    /* Restore interrupt state */
    if (EnableInterrupts) EFlags |= EFLAGS_INTERRUPT_MASK;
    __writeeflags(EFlags);
}


/* SOFTWARE INTERRUPT TRAPS ***************************************************/

#ifndef _M_AMD64
VOID
DECLSPEC_NORETURN
FASTCALL
HalpApcInterruptHandler(IN PKTRAP_FRAME TrapFrame)
{
    KPROCESSOR_MODE ProcessorMode;
    KIRQL OldIrql;
    ASSERT(ApicGetProcessorIrql() == APC_LEVEL);

   /* Enter trap */
    KiEnterInterruptTrap(TrapFrame);

#ifdef APIC_LAZY_IRQL
    if (ApicGetCurrentIrql() >= APC_LEVEL)
    {
        HalBeginSystemInterrupt(APC_LEVEL, APC_VECTOR, &OldIrql);

        /* "Spurious" interrupt, exit the interrupt */
        KiEoiHelper(TrapFrame);
    }
#endif
    /* Save the old IRQL */
    OldIrql = ApicGetCurrentIrql();
    ASSERT(OldIrql < APC_LEVEL);

    /* Raise to APC_LEVEL */
    ApicRaiseIrql(APC_LEVEL);

    /* End the interrupt */
    ApicSendEOI();

    /* Kernel or user APC? */
    if (KiUserTrap(TrapFrame)) ProcessorMode = UserMode;
    else if (TrapFrame->EFlags & EFLAGS_V86_MASK) ProcessorMode = UserMode;
    else ProcessorMode = KernelMode;

    /* Enable interrupts and call the kernel's APC interrupt handler */
    _enable();
    KiDeliverApc(ProcessorMode, NULL, TrapFrame);

    /* Disable interrupts */
    _disable();

    /* Restore the old IRQL */
    ApicLowerIrql(OldIrql);

    /* Exit the interrupt */
    KiEoiHelper(TrapFrame);
}

VOID
DECLSPEC_NORETURN
FASTCALL
HalpDispatchInterruptHandler(IN PKTRAP_FRAME TrapFrame)
{
    KIRQL OldIrql;
    ASSERT(ApicGetProcessorIrql() == DISPATCH_LEVEL);

   /* Enter trap */
    KiEnterInterruptTrap(TrapFrame);

#ifdef APIC_LAZY_IRQL
    if (!HalBeginSystemInterrupt(DISPATCH_LEVEL, DISPATCH_VECTOR, &OldIrql))
    {
        /* "Spurious" interrupt, exit the interrupt */
        KiEoiHelper(TrapFrame);
    }
#else
    /* Get the current IRQL */
    OldIrql = ApicGetCurrentIrql();
    ASSERT(OldIrql < DISPATCH_LEVEL);
#endif

    /* Raise to DISPATCH_LEVEL */
    ApicRaiseIrql(DISPATCH_LEVEL);

    /* End the interrupt */
    ApicSendEOI();

    /* Enable interrupts and call the kernel's DPC interrupt handler */
    _enable();
    KiDispatchInterrupt();
    _disable();

    /* Restore the old IRQL */
    ApicLowerIrql(OldIrql);

    /* Exit the interrupt */
    KiEoiHelper(TrapFrame);
}
#endif


/* SOFTWARE INTERRUPTS ********************************************************/


VOID
FASTCALL
HalRequestSoftwareInterrupt(IN KIRQL Irql)
{
    /* Convert irql to vector and request an interrupt */
    ApicRequestSelfInterrupt(IrqlToSoftVector(Irql), APIC_TGM_Edge);
}

VOID
FASTCALL
HalClearSoftwareInterrupt(
    IN KIRQL Irql)
{
    /* Nothing to do */
}


/* SYSTEM INTERRUPTS **********************************************************/

static PKINTERRUPT HalpPassiveLines[256];

static
VOID
HalpSetLineMask(
    _In_ ULONG Vector,
    _In_ BOOLEAN Masked)
{
    IOAPIC_REDIRECTION_REGISTER ReDirReg;
    PHALP_IOAPIC IoApic;
    UCHAR Pin;

    IoApic = HalpIoApicFromGsi(HalpVectorToIndex[Vector], &Pin);
    if (IoApic == NULL)
        return;
    HalpAcquireIoApicPairLock();
    ReDirReg.Long0 = IOApicReadRaw(IoApic->Base, IOAPIC_REDTBL + 2 * Pin);
    ReDirReg.Mask = Masked;
    IOApicWriteRaw(IoApic->Base, IOAPIC_REDTBL + 2 * Pin, ReDirReg.Long0);
    HalpReleaseIoApicPairLock();
}

static
BOOLEAN
NTAPI
HalpPassiveLineService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    UNREFERENCED_PARAMETER(ServiceContext);

    if (Interrupt->Mode == LevelSensitive)
        HalpSetLineMask(Interrupt->Vector, TRUE);
    KeDispatchSecondaryInterrupt(Interrupt->Vector, 0, NULL);
    return TRUE;
}

static
BOOLEAN
HalpEnablePassiveLine(
    _In_ ULONG Vector,
    _In_ KINTERRUPT_MODE InterruptMode)
{
    PKINTERRUPT Interrupt;
    KIRQL Irql;

    if (Vector >= RTL_NUMBER_OF(HalpPassiveLines) || HalpVectorToIndex[Vector] >= HalpIoApicMaxIrq)
        return FALSE;
    if (HalpPassiveLines[Vector] != NULL)
    {
        HalpSetLineMask(Vector, FALSE);
        return TRUE;
    }
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;

    Interrupt = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Interrupt), TAG_HAL);
    if (Interrupt == NULL)
        return FALSE;
    Irql = HalpVectorToIrql((UCHAR)Vector);
    KeInitializeInterrupt(Interrupt,
                          HalpPassiveLineService,
                          NULL,
                          &Interrupt->SpinLock,
                          Vector,
                          Irql,
                          Irql,
                          InterruptMode,
                          FALSE,
                          0,
                          FALSE);
    HalpPassiveLines[Vector] = Interrupt;
    if (!KeConnectInterrupt(Interrupt))
    {
        HalpPassiveLines[Vector] = NULL;
        ExFreePoolWithTag(Interrupt, TAG_HAL);
        return FALSE;
    }
    return TRUE;
}

static
VOID
HalpDisablePassiveLine(
    _In_ ULONG Vector)
{
    PKINTERRUPT Interrupt;

    if (Vector >= RTL_NUMBER_OF(HalpPassiveLines))
        return;
    Interrupt = HalpPassiveLines[Vector];
    if (Interrupt == NULL)
        return;
    KeDisconnectInterrupt(Interrupt);
    HalpPassiveLines[Vector] = NULL;
    ExFreePoolWithTag(Interrupt, TAG_HAL);
}

BOOLEAN
NTAPI
HalEnableSystemInterrupt(
    IN ULONG Vector,
    IN KIRQL Irql,
    IN KINTERRUPT_MODE InterruptMode)
{
    IOAPIC_REDIRECTION_REGISTER ReDirReg;
    UCHAR Index;
    ASSERT(Irql <= HIGH_LEVEL);

    if (HalpSecondaryIsVector(Vector))
        return HalpSecondaryEnable(Vector, Irql, InterruptMode);
    if (Irql == PASSIVE_LEVEL)
        return HalpEnablePassiveLine(Vector, InterruptMode);

    ASSERT((IrqlToTpr(Irql) & 0xF0) == (Vector & 0xF0));

    /* Get the irq for this vector */
    Index = HalpVectorToIndex[Vector];

    /* Check if its valid */
    if (Index == APIC_FREE_VECTOR)
    {
        /* Interrupt is not in use */
        return FALSE;
    }

    /* Cherry-picked from dev branch (f08c89371ff): an MSI/MSI-X vector
     * marked as RESERVED has no IO-APIC redirection entry to program.
     * The driver may try to enable the vector again when falling back
     * from MSI to INTx with the same vector — treat that as success
     * (idempotent enable) instead of indexing past HalpVectorToIndex
     * with the sentinel 0xFE. */
    if (Index == APIC_RESERVED_VECTOR)
    {
        DPRINT("HalEnableSystemInterrupt: Vector=%lu is reserved (MSI/MSI-X)\n",
                Vector);
        return TRUE;
    }

    /* Read the redirection entry */
    ReDirReg = ApicReadIORedirectionEntry(Index);

    /* Check if the interrupt is already enabled */
    if (ReDirReg.Mask == FALSE)
    {
        /* If the vector does not match, something is wrong */
        if (ReDirReg.Vector != Vector) return FALSE;

        /* Repair stale routing: anything but fixed physical delivery can hit a CPU without the IDT handler, where the vector is swallowed without EOI and wedges that CPU's ISR/PPR */
        if ((ReDirReg.MessageType != APIC_MT_Fixed) || (ReDirReg.DestinationMode != APIC_DM_Physical))
        {
            DPRINT1("HalEnableSystemInterrupt: repairing RTE[%u] vector 0x%lx routing (was MT %u DM %u dest 0x%x)\n", Index, Vector, ReDirReg.MessageType, ReDirReg.DestinationMode, ReDirReg.Destination);
            ReDirReg.MessageType = APIC_MT_Fixed;
            ReDirReg.DestinationMode = APIC_DM_Physical;
            ReDirReg.Destination = ApicRead(APIC_ID) >> 24;
            ApicWriteIORedirectionEntry(Index, ReDirReg);
        }
        return TRUE;
    }

    /* Set up the redirection entry */
    ReDirReg.Vector = Vector;
    ReDirReg.MessageType = APIC_MT_Fixed;
    ReDirReg.DestinationMode = APIC_DM_Physical;
    ReDirReg.Destination = ApicRead(APIC_ID) >> 24;
    ReDirReg.TriggerMode = (InterruptMode == LevelSensitive) ? APIC_TGM_Level : APIC_TGM_Edge;
    ReDirReg.Mask = FALSE;

    /* Pin shape: MADT overrides are authoritative for ISA-sourced GSIs,
     * the _PRT for PCI GSIs; otherwise bus convention (level = PCI INTx =
     * active-low, edge = ISA = active-high). */
    {
        BOOLEAN ActiveLow;
        BOOLEAN LevelTriggered;

        if (HalpQueryMadtRteHints(Index, &ActiveLow, &LevelTriggered) || HalpQueryPciGsiRteHints(Index, &ActiveLow, &LevelTriggered))
        {
            ReDirReg.Polarity = ActiveLow ? 1 : 0;
            ReDirReg.TriggerMode = LevelTriggered ? APIC_TGM_Level : APIC_TGM_Edge;
        }
        else
        {
            ReDirReg.Polarity = (ReDirReg.TriggerMode == APIC_TGM_Level) ? 1 : 0;
        }
    }

    /* Write back the entry */
    ApicWriteIORedirectionEntry(Index, ReDirReg);

    DPRINT("HalEnableSystemInterrupt: RTE[%u] vector 0x%lx -> phys APIC 0x%x (%s)\n", Index, Vector, ReDirReg.Destination, ReDirReg.TriggerMode ? "level" : "edge");

    return TRUE;
}

VOID
NTAPI
HalDisableSystemInterrupt(
    IN ULONG Vector,
    IN KIRQL Irql)
{
    IOAPIC_REDIRECTION_REGISTER ReDirReg;
    PHALP_IOAPIC IoApic;
    UCHAR Index;
    UCHAR Pin;
    ASSERT(Irql <= HIGH_LEVEL);
    ASSERT(Vector < RTL_NUMBER_OF(HalpVectorToIndex));

    if (HalpSecondaryIsVector(Vector))
    {
        HalpSecondaryDisable(Vector);
        return;
    }
    if (Irql == PASSIVE_LEVEL)
    {
        HalpDisablePassiveLine(Vector);
        return;
    }

    Index = HalpVectorToIndex[Vector];
    if (Index == APIC_RESERVED_VECTOR)
        return;

    /* No IOAPIC line behind this vector; a blind RMW through the stale
       index would target an unrelated register. */
    IoApic = HalpIoApicFromGsi(Index, &Pin);
    if (IoApic == NULL)
    {
        DPRINT1("HalDisableSystemInterrupt: vector 0x%lx has no IOAPIC line (index 0x%x)\n", Vector, Index);
        return;
    }

    /* Read lower dword of redirection entry */
    ReDirReg.Long0 = IOApicRead(IoApic->Base, IOAPIC_REDTBL + 2 * Pin);

    /* Mask it */
    ReDirReg.Mask = 1;

    /* Write back lower dword */
    IOApicWrite(IoApic->Base, IOAPIC_REDTBL + 2 * Pin, ReDirReg.Long0);
}

BOOLEAN
NTAPI
HalBeginSystemInterrupt(
    IN KIRQL Irql,
    IN ULONG Vector,
    OUT PKIRQL OldIrql)
{
    KIRQL CurrentIrql;

    /* Get the current IRQL */
    CurrentIrql = ApicGetCurrentIrql();

#ifdef APIC_LAZY_IRQL
    /* Check if this interrupt is allowed */
    if (CurrentIrql >= Irql)
    {
        IOAPIC_REDIRECTION_REGISTER RedirReg;
        UCHAR Index;

        /* It is not, set the real Irql in the TPR! */
        ApicWrite(APIC_TPR, IrqlToTpr(CurrentIrql));

        /* Save the new hard IRQL in the IRR field */
        KeGetPcr()->IRR = CurrentIrql;

        /* End this interrupt */
        ApicSendEOI();

        /* Get the irq for this vector */
        Index = HalpVectorToIndex[Vector];

        /* Check if it's valid */
        if (Index < HalpIoApicMaxIrq)
        {
            /* Read the I/O redirection entry */
            RedirReg = ApicReadIORedirectionEntry(Index);

            /* Re-request the interrupt to be handled later */
            ApicRequestSelfInterrupt(Vector, (UCHAR)RedirReg.TriggerMode);
       }
       else
       {
            /* This should be a reserved vector! */
            ASSERT(Index == APIC_RESERVED_VECTOR);

            /* Re-request the interrupt to be handled later */
            ApicRequestSelfInterrupt(Vector, APIC_TGM_Edge);
       }

        /* Pretend it was a spurious interrupt */
        return FALSE;
    }
#endif
    /* Save the current IRQL */
    *OldIrql = CurrentIrql;

    /* Set the new IRQL */
    ApicRaiseIrql(Irql);

    /* Turn on interrupts */
    _enable();

    /* Success */
    return TRUE;
}

VOID
NTAPI
HalEndSystemInterrupt(
    IN KIRQL OldIrql,
    IN PKTRAP_FRAME TrapFrame)
{
    /* Send an EOI */
    ApicSendEOI();

    /* Restore the old IRQL */
    ApicLowerIrql(OldIrql);
}


/* IRQL MANAGEMENT ************************************************************/

#ifndef _M_AMD64
KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    /* Read the current TPR and convert it to an IRQL */
    return ApicGetCurrentIrql();
}

VOID
FASTCALL
KfLowerIrql(
    IN KIRQL OldIrql)
{
#if DBG
    /* Validate correct lower */
    if (OldIrql > ApicGetCurrentIrql())
    {
        /* Crash system */
        KeBugCheck(IRQL_NOT_LESS_OR_EQUAL);
    }
#endif
    /* Set the new IRQL */
    ApicLowerIrql(OldIrql);
}

KIRQL
FASTCALL
KfRaiseIrql(
    IN KIRQL NewIrql)
{
    KIRQL OldIrql;

    /* Read the current IRQL */
    OldIrql = ApicGetCurrentIrql();
#if DBG
    /* Validate correct raise */
    if (OldIrql > NewIrql)
    {
        /* Crash system */
        KeBugCheck(IRQL_NOT_GREATER_OR_EQUAL);
    }
#endif
    /* Convert the new IRQL to a TPR value and write the register */
    ApicRaiseIrql(NewIrql);

    /* Return old IRQL */
    return OldIrql;
}

KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID)
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

KIRQL
NTAPI
KeRaiseIrqlToSynchLevel(VOID)
{
    return KfRaiseIrql(SYNCH_LEVEL);
}

#endif /* !_M_AMD64 */
