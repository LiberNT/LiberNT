/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Apple AIC2/AIC3 interrupt backend with timer and fast-IPI FIQs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <reactos/arm64/sysreg.h>
#include "devtree.h"
#include "halext.h"
#include "apple_aic.h"
#include <reactos/drivers/applepcie.h>

#define NDEBUG
#include <debug.h>

#define AIC_INFO1                0x0004
#define AIC_INFO3                0x000C
#define AIC_CONFIG               0x0014
#define AIC_CONFIG_ENABLE        0x00000001
#define AIC2_IRQ_CFG             0x00002000
#define AIC3_IRQ_CFG             0x00010000
#define AIC_EVENT_TYPE_IRQ       1
#define AIC_MAX_DIES             8
#define AIC_MAX_DEVICE_IRQS      4096
#define AIC_FIRST_DEVICE_INTID   32
#define AIC_SPURIOUS_INTID       (AIC_FIRST_DEVICE_INTID + AIC_MAX_DEVICE_IRQS)
#define AIC_SGI_COUNT            4
#define AIC_DEFAULT_PRIORITY     0xA0
#define AIC_TIMER_V_INTID        27
#define AIC_TIMER_P_INTID        30
#define AIC_CNT_CTL_ENABLE       0x1ULL
#define AIC_CNT_CTL_IMASK        0x2ULL
#define AIC_CNT_CTL_ISTATUS      0x4ULL
#define AIC_IPI_SR_PENDING       0x1ULL
#define AIC_PMCR0_IMODE          0x700ULL
#define AIC_PMCR0_IACT           0x800ULL

C_ASSERT(AIC_FIRST_DEVICE_INTID + AIC_MAX_DEVICE_IRQS <= HAL_ARM64_CONTROLLER_INTID_MAX);

BOOLEAN AppleAicDetected = FALSE;

static ULONGLONG AppleAicCoreBase;
static ULONGLONG AppleAicCorePhysical;
static ULONGLONG AppleAicCoreLength;
static ULONGLONG AppleAicEventBase;
static ULONGLONG AppleAicEventPhysical;
static ULONGLONG AppleAicEventLength;
static ULONG AppleAicIrqCfgOffset;
static ULONG AppleAicMaxIrq;
static ULONG AppleAicDieCount;
static ULONG AppleAicDieStride;
static ULONG AppleAicMaskSetOffset;
static ULONG AppleAicMaskClrOffset;
static ULONG AppleAicSwSetOffset;
static ULONG AppleAicSwClrOffset;

static volatile UCHAR AppleAicPmr[MAXIMUM_PROCESSORS];
static volatile LONG AppleAicPendingSgi[MAXIMUM_PROCESSORS];
static volatile UCHAR AppleAicTimerDeferred[MAXIMUM_PROCESSORS];
static volatile UCHAR AppleAicTimerEnabled[MAXIMUM_PROCESSORS];
static ULONGLONG AppleAicMpidr[MAXIMUM_PROCESSORS];

static UCHAR AppleAicSgiPriority[AIC_SGI_COUNT] =
{
    HAL_ARM64_SGI_IPI_PRIORITY,
    HAL_ARM64_SGI_APC_PRIORITY,
    HAL_ARM64_SGI_DPC_PRIORITY,
    HAL_ARM64_SGI_FREEZE_PRIORITY
};
static UCHAR AppleAicTimerPriority[2];
static UCHAR AppleAicDevicePriority[AIC_MAX_DEVICE_IRQS];
static volatile LONG AppleAicDeviceEnabled[AIC_MAX_DEVICE_IRQS / 32];
static volatile LONG AppleAicDeviceDeferred[AIC_MAX_DEVICE_IRQS / 32];
static volatile LONG AppleAicDeviceDeferredAny;
static volatile LONG AppleAicDeviceEdge[AIC_MAX_DEVICE_IRQS / 32];

static const UCHAR AppleAicIrqlToPmr[HIGH_LEVEL + 1] =
{
    0xFF, 0xE0, 0xD0,
    0xC0, 0xB0, 0xA0, 0x90, 0x80,
    0x70, 0x60, 0x50, 0x40, 0x30,
    0x20,
    0x10,
    0x00
};

FORCEINLINE
ULONGLONG
AppleAicMaskInterrupts(VOID)
{
    ULONGLONG Daif = ARM64_READ_SYSREG(DAIF);

    ARM64_DAIFSET(3);
    return Daif;
}

FORCEINLINE
VOID
AppleAicRestoreInterrupts(
    _In_ ULONGLONG Daif)
{
    ARM64_WRITE_SYSREG(DAIF, Daif);
}

FORCEINLINE
ULONG
AppleAicCurrentCpu(VOID)
{
    ULONG Cpu = KeGetCurrentProcessorNumber();

    return (Cpu < MAXIMUM_PROCESSORS) ? Cpu : 0;
}

static
ULONG
AppleAicRead(
    _In_ ULONG Offset)
{
    return *HalpMmio((ULONG_PTR)AppleAicCoreBase, Offset);
}

static
VOID
AppleAicWrite(
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    *HalpMmio((ULONG_PTR)AppleAicCoreBase, Offset) = Value;
}

static
ULONG
AppleAicReadEvent(VOID)
{
    return *HalpMmio((ULONG_PTR)AppleAicEventBase, 0);
}

static
ULONG
AppleAicWordOffset(
    _In_ ULONG ArrayOffset,
    _In_ ULONG Index)
{
    ULONG Die = Index / AppleAicMaxIrq;
    ULONG Number = Index % AppleAicMaxIrq;

    return AppleAicIrqCfgOffset + (Die * AppleAicDieStride) + ArrayOffset + (4 * (Number >> 5));
}

FORCEINLINE
ULONG
AppleAicBit(
    _In_ ULONG Index)
{
    return 1u << ((Index % AppleAicMaxIrq) & 31);
}

static
VOID
AppleAicMaskDevice(
    _In_ ULONG Index,
    _In_ BOOLEAN Mask)
{
    AppleAicWrite(AppleAicWordOffset(Mask ? AppleAicMaskSetOffset : AppleAicMaskClrOffset, Index),
                  AppleAicBit(Index));
}

static
VOID
AppleAicSetSoftwareInterrupt(
    _In_ ULONG Index,
    _In_ BOOLEAN Pending)
{
    AppleAicWrite(AppleAicWordOffset(Pending ? AppleAicSwSetOffset : AppleAicSwClrOffset, Index),
                  AppleAicBit(Index));
}

static
ULONGLONG
AppleAicReadTimerControl(
    _In_ ULONG IntId)
{
    return (IntId == AIC_TIMER_V_INTID) ? ARM64_READ_SYSREG(CNTV_CTL_EL0) : ARM64_READ_SYSREG(CNTP_CTL_EL0);
}

static
VOID
AppleAicWriteTimerControl(
    _In_ ULONG IntId,
    _In_ ULONGLONG Control)
{
    if (IntId == AIC_TIMER_V_INTID)
        ARM64_WRITE_SYSREG(CNTV_CTL_EL0, Control);
    else
        ARM64_WRITE_SYSREG(CNTP_CTL_EL0, Control);
    ARM64_ISB();
}

static
BOOLEAN
AppleAicTimerFiring(
    _In_ ULONG IntId)
{
    ULONGLONG Control = AppleAicReadTimerControl(IntId);

    return (Control & AIC_CNT_CTL_ENABLE) && !(Control & AIC_CNT_CTL_IMASK) && (Control & AIC_CNT_CTL_ISTATUS);
}

static
VOID
AppleAicMaskTimer(
    _In_ ULONG IntId,
    _In_ BOOLEAN Mask)
{
    ULONGLONG Control = AppleAicReadTimerControl(IntId);

    if (!(Control & AIC_CNT_CTL_ENABLE))
        return;
    Control = Mask ? (Control | AIC_CNT_CTL_IMASK) : (Control & ~AIC_CNT_CTL_IMASK);
    AppleAicWriteTimerControl(IntId, Control);
}

FORCEINLINE
ULONG
AppleAicTimerSlot(
    _In_ ULONG IntId)
{
    return (IntId == AIC_TIMER_V_INTID) ? 0 : 1;
}

static
VOID
AppleAicSendFastIpi(
    _In_ ULONG Cpu)
{
    ULONGLONG Target = AppleAicMpidr[Cpu];
    ULONGLONG Self = ARM64_READ_SYSREG(MPIDR_EL1);

    ARM64_DSB(SY);
    if (((Target >> 8) & 0xFF) == ((Self >> 8) & 0xFF))
        ARM64_WRITE_SYSREG(S3_5_C15_C0_0, Target & 0xFF);
    else
        ARM64_WRITE_SYSREG(S3_5_C15_C0_1, (Target & 0xFF) | (((Target >> 8) & 0xFF) << 16));
    ARM64_ISB();
}

static
ULONG
AppleAicAcknowledgeInterrupt(VOID)
{
    ULONG Cpu = AppleAicCurrentCpu();
    UCHAR Pmr = AppleAicPmr[Cpu];
    UCHAR BestPriority = Pmr;
    ULONG BestSgi = AIC_SPURIOUS_INTID;
    ULONG Event;
    ULONG Index;
    ULONG Sgi;
    ULONG IntId;
    LONG Pending;

    if (ARM64_READ_SYSREG(S3_5_C15_C1_1) & AIC_IPI_SR_PENDING)
    {
        ARM64_WRITE_SYSREG(S3_5_C15_C1_1, AIC_IPI_SR_PENDING);
        ARM64_ISB();
    }

    Pending = AppleAicPendingSgi[Cpu];
    for (Sgi = 0; Sgi < AIC_SGI_COUNT; Sgi++)
    {
        if ((Pending & (1 << Sgi)) && AppleAicSgiPriority[Sgi] < BestPriority)
        {
            BestPriority = AppleAicSgiPriority[Sgi];
            BestSgi = Sgi;
        }
    }
    if (BestSgi != AIC_SPURIOUS_INTID)
    {
        InterlockedAnd(&AppleAicPendingSgi[Cpu], ~(1 << BestSgi));
        return BestSgi;
    }

    for (IntId = AIC_TIMER_V_INTID; IntId <= AIC_TIMER_P_INTID; IntId += (AIC_TIMER_P_INTID - AIC_TIMER_V_INTID))
    {
        if (!AppleAicTimerFiring(IntId))
            continue;
        AppleAicMaskTimer(IntId, TRUE);
        if (AppleAicTimerPriority[AppleAicTimerSlot(IntId)] < Pmr)
            return IntId;
        AppleAicTimerDeferred[Cpu] |= (UCHAR)(1u << AppleAicTimerSlot(IntId));
    }

    if (!AppleAicEventBase)
        return AIC_SPURIOUS_INTID;
    Event = AppleAicReadEvent();
    if (((Event >> 16) & 0xFF) != AIC_EVENT_TYPE_IRQ || (Event & 0xFFFF) >= AppleAicMaxIrq)
        return AIC_SPURIOUS_INTID;
    Index = (((Event >> 24) & 0xFF) * AppleAicMaxIrq) + (Event & 0xFFFF);
    if (Index >= AIC_MAX_DEVICE_IRQS)
        return AIC_SPURIOUS_INTID;
    if (AppleAicDevicePriority[Index] < Pmr)
    {
        if ((ULONG)AppleAicDeviceEdge[Index / 32] & (1u << (Index % 32)))
            AppleAicSetSoftwareInterrupt(Index, FALSE);
        return AIC_FIRST_DEVICE_INTID + Index;
    }
    if ((ULONG)AppleAicDeviceEdge[Index / 32] & (1u << (Index % 32)))
        AppleAicSetSoftwareInterrupt(Index, TRUE);
    InterlockedOr(&AppleAicDeviceDeferred[Index / 32], (LONG)(1u << (Index % 32)));
    InterlockedExchange(&AppleAicDeviceDeferredAny, 1);
    return AIC_SPURIOUS_INTID;
}

static
VOID
AppleAicEndInterrupt(
    _In_ ULONG IntId)
{
    ULONG Index;

    if (IntId == AIC_TIMER_V_INTID || IntId == AIC_TIMER_P_INTID)
    {
        if (AppleAicTimerEnabled[AppleAicCurrentCpu()] & (1u << AppleAicTimerSlot(IntId)))
            AppleAicMaskTimer(IntId, FALSE);
        return;
    }
    if (IntId < AIC_FIRST_DEVICE_INTID || IntId >= AIC_FIRST_DEVICE_INTID + AIC_MAX_DEVICE_IRQS)
        return;
    Index = IntId - AIC_FIRST_DEVICE_INTID;
    if ((ULONG)AppleAicDeviceEnabled[Index / 32] & (1u << (Index % 32)))
        AppleAicMaskDevice(Index, FALSE);
}

static
VOID
AppleAicEnableInterrupt(
    _In_ ULONG IntId)
{
    ULONGLONG Daif;
    ULONG Index;

    if (IntId == AIC_TIMER_V_INTID || IntId == AIC_TIMER_P_INTID)
    {
        Daif = AppleAicMaskInterrupts();
        AppleAicTimerDeferred[AppleAicCurrentCpu()] &= (UCHAR)~(1u << AppleAicTimerSlot(IntId));
        AppleAicTimerEnabled[AppleAicCurrentCpu()] |= (UCHAR)(1u << AppleAicTimerSlot(IntId));
        AppleAicMaskTimer(IntId, FALSE);
        AppleAicRestoreInterrupts(Daif);
        return;
    }
    if (IntId < AIC_FIRST_DEVICE_INTID || IntId >= AIC_FIRST_DEVICE_INTID + AIC_MAX_DEVICE_IRQS)
        return;
    Index = IntId - AIC_FIRST_DEVICE_INTID;
    if (Index >= AppleAicDieCount * AppleAicMaxIrq)
        return;
    InterlockedOr(&AppleAicDeviceEnabled[Index / 32], (LONG)(1u << (Index % 32)));
    InterlockedAnd(&AppleAicDeviceDeferred[Index / 32], (LONG)~(1u << (Index % 32)));
    AppleAicMaskDevice(Index, FALSE);
}

static
VOID
AppleAicDisableInterrupt(
    _In_ ULONG IntId)
{
    ULONGLONG Daif;
    ULONG Index;

    if (IntId == AIC_TIMER_V_INTID || IntId == AIC_TIMER_P_INTID)
    {
        Daif = AppleAicMaskInterrupts();
        AppleAicTimerDeferred[AppleAicCurrentCpu()] &= (UCHAR)~(1u << AppleAicTimerSlot(IntId));
        AppleAicTimerEnabled[AppleAicCurrentCpu()] &= (UCHAR)~(1u << AppleAicTimerSlot(IntId));
        AppleAicMaskTimer(IntId, TRUE);
        AppleAicRestoreInterrupts(Daif);
        return;
    }
    if (IntId < AIC_FIRST_DEVICE_INTID || IntId >= AIC_FIRST_DEVICE_INTID + AIC_MAX_DEVICE_IRQS)
        return;
    Index = IntId - AIC_FIRST_DEVICE_INTID;
    if (Index >= AppleAicDieCount * AppleAicMaxIrq)
        return;
    InterlockedAnd(&AppleAicDeviceEnabled[Index / 32], (LONG)~(1u << (Index % 32)));
    InterlockedAnd(&AppleAicDeviceDeferred[Index / 32], (LONG)~(1u << (Index % 32)));
    AppleAicMaskDevice(Index, TRUE);
}

static
VOID
AppleAicSetInterruptPriority(
    _In_ ULONG IntId,
    _In_ ULONG Priority)
{
    if (IntId < AIC_SGI_COUNT)
        AppleAicSgiPriority[IntId] = (UCHAR)Priority;
    else if (IntId == AIC_TIMER_V_INTID || IntId == AIC_TIMER_P_INTID)
        AppleAicTimerPriority[AppleAicTimerSlot(IntId)] = (UCHAR)Priority;
    else if (IntId >= AIC_FIRST_DEVICE_INTID && IntId < AIC_FIRST_DEVICE_INTID + AIC_MAX_DEVICE_IRQS)
        AppleAicDevicePriority[IntId - AIC_FIRST_DEVICE_INTID] = (UCHAR)Priority;
}

ULONG
HalpAppleAicIrqsPerDie(VOID)
{
    return AppleAicMaxIrq;
}

static
VOID
AppleAicSetTriggerMode(
    _In_ ULONG IntId,
    _In_ BOOLEAN EdgeTriggered)
{
    ULONG Index;

    if (IntId < AIC_FIRST_DEVICE_INTID || IntId >= AIC_FIRST_DEVICE_INTID + AIC_MAX_DEVICE_IRQS)
        return;
    Index = IntId - AIC_FIRST_DEVICE_INTID;
    if (EdgeTriggered)
        InterlockedOr(&AppleAicDeviceEdge[Index / 32], (LONG)(1u << (Index % 32)));
    else
        InterlockedAnd(&AppleAicDeviceEdge[Index / 32], (LONG)~(1u << (Index % 32)));
}

static
VOID
AppleAicSetPriorityMask(
    _In_ KIRQL Irql)
{
    UCHAR Pmr = AppleAicIrqlToPmr[(Irql <= HIGH_LEVEL) ? Irql : HIGH_LEVEL];
    ULONGLONG Daif;
    ULONG Cpu;
    ULONG Slot;
    ULONG Sgi;
    ULONG Word;
    LONG Pending;

    Daif = AppleAicMaskInterrupts();
    Cpu = AppleAicCurrentCpu();
    AppleAicPmr[Cpu] = Pmr;
    ARM64_DSB(SY);

    if (AppleAicTimerDeferred[Cpu] != 0)
    {
        for (Slot = 0; Slot < 2; Slot++)
        {
            if ((AppleAicTimerDeferred[Cpu] & (1u << Slot)) && AppleAicTimerPriority[Slot] < Pmr)
            {
                AppleAicTimerDeferred[Cpu] &= (UCHAR)~(1u << Slot);
                AppleAicMaskTimer(Slot ? AIC_TIMER_P_INTID : AIC_TIMER_V_INTID, FALSE);
            }
        }
    }

    Pending = AppleAicPendingSgi[Cpu];
    for (Sgi = 0; Sgi < AIC_SGI_COUNT; Sgi++)
    {
        if ((Pending & (1 << Sgi)) && AppleAicSgiPriority[Sgi] < Pmr)
        {
            AppleAicSendFastIpi(Cpu);
            break;
        }
    }

    if (AppleAicDeviceDeferredAny && InterlockedExchange(&AppleAicDeviceDeferredAny, 0))
    {
        for (Word = 0; Word < (AppleAicDieCount * AppleAicMaxIrq + 31) / 32; Word++)
        {
            ULONG Deferred = (ULONG)AppleAicDeviceDeferred[Word];

            while (Deferred != 0)
            {
                ULONG Bit = Deferred & (0 - Deferred);
                ULONG Index = (Word * 32) + (ULONG)RtlFindLeastSignificantBit(Bit);

                Deferred &= ~Bit;
                if (AppleAicDevicePriority[Index] >= Pmr)
                {
                    InterlockedExchange(&AppleAicDeviceDeferredAny, 1);
                    continue;
                }
                InterlockedAnd(&AppleAicDeviceDeferred[Word], (LONG)~Bit);
                if ((ULONG)AppleAicDeviceEnabled[Word] & Bit)
                    AppleAicMaskDevice(Index, FALSE);
            }
        }
    }

    AppleAicRestoreInterrupts(Daif);
}

static
ULONG
AppleAicGetPriorityMask(VOID)
{
    return AppleAicPmr[AppleAicCurrentCpu()];
}

static
VOID
AppleAicSendSgi(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG SgiId)
{
    ULONG Cpu;

    if (SgiId >= AIC_SGI_COUNT)
        return;
    for (Cpu = 0; Cpu < MAXIMUM_PROCESSORS && Cpu < 64; Cpu++)
    {
        if (!(TargetSet & ((KAFFINITY)1 << Cpu)))
            continue;
        InterlockedOr(&AppleAicPendingSgi[Cpu], 1 << SgiId);
        AppleAicSendFastIpi(Cpu);
    }
}

static
VOID
AppleAicInitializeProcessor(
    _In_ ULONG ProcessorNumber)
{
    ULONG Cpu = (ProcessorNumber < MAXIMUM_PROCESSORS) ? ProcessorNumber : 0;

    AppleAicMpidr[Cpu] = ARM64_READ_SYSREG(MPIDR_EL1);
    AppleAicPmr[Cpu] = 0x00;
    AppleAicPendingSgi[Cpu] = 0;
    AppleAicTimerDeferred[Cpu] = 0;
    ARM64_WRITE_SYSREG(S3_5_C15_C1_1, AIC_IPI_SR_PENDING);
    ARM64_WRITE_SYSREG(S3_1_C15_C0_0, ARM64_READ_SYSREG(S3_1_C15_C0_0) & ~(AIC_PMCR0_IMODE | AIC_PMCR0_IACT));
    ARM64_ISB();
}

static
BOOLEAN
AppleAicPhase0Initialize(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG Info1;
    ULONG Info3;
    ULONG Words;
    ULONG Die;
    ULONG Word;

    UNREFERENCED_PARAMETER(LoaderBlock);

    Info1 = AppleAicRead(AIC_INFO1);
    Info3 = AppleAicRead(AIC_INFO3);
    AppleAicDieCount = ((Info1 >> 24) & 0xF) + 1;
    AppleAicMaxIrq = Info3 & 0xFFFF;
    if (AppleAicMaxIrq == 0 || (AppleAicMaxIrq % 32) != 0 || AppleAicDieCount > AIC_MAX_DIES ||
        AppleAicDieCount * AppleAicMaxIrq > AIC_MAX_DEVICE_IRQS)
    {
        DPRINT1("[AIC] unsupported geometry: INFO1 %08lx INFO3 %08lx\n", Info1, Info3);
        return FALSE;
    }

    Words = AppleAicMaxIrq / 32;
    AppleAicSwSetOffset = 4 * AppleAicMaxIrq;
    AppleAicSwClrOffset = AppleAicSwSetOffset + (4 * Words);
    AppleAicMaskSetOffset = AppleAicSwClrOffset + (4 * Words);
    AppleAicMaskClrOffset = AppleAicMaskSetOffset + (4 * Words);
    AppleAicDieStride = (4 * AppleAicMaxIrq) + (5 * 4 * Words);

    for (Die = 0; Die < AppleAicDieCount; Die++)
    {
        for (Word = 0; Word < Words; Word++)
        {
            AppleAicWrite(AppleAicIrqCfgOffset + (Die * AppleAicDieStride) + AppleAicMaskSetOffset + (4 * Word),
                          0xFFFFFFFF);
            AppleAicWrite(AppleAicIrqCfgOffset + (Die * AppleAicDieStride) + AppleAicSwClrOffset + (4 * Word),
                          0xFFFFFFFF);
        }
    }
    AppleAicWrite(AIC_CONFIG, AppleAicRead(AIC_CONFIG) | AIC_CONFIG_ENABLE);

    AppleAicInitializeProcessor(KeGetCurrentProcessorNumber());

    DPRINT1("[AIC] %lu die(s), %lu IRQs per die, core %I64x event %I64x\n",
            AppleAicDieCount, AppleAicMaxIrq, AppleAicCorePhysical, AppleAicEventPhysical);
    return TRUE;
}

static
BOOLEAN
AppleAicPhase1MapRuntimeMmio(VOID)
{
    PHYSICAL_ADDRESS Physical;
    PVOID Mapped;

    Physical.QuadPart = (LONGLONG)AppleAicCorePhysical;
    Mapped = MmMapIoSpace(Physical, (SIZE_T)AppleAicCoreLength, MmNonCached);
    if (!Mapped)
        return FALSE;
    AppleAicCoreBase = (ULONGLONG)(ULONG_PTR)Mapped;

    Physical.QuadPart = (LONGLONG)AppleAicEventPhysical;
    Mapped = MmMapIoSpace(Physical, (SIZE_T)AppleAicEventLength, MmNonCached);
    if (!Mapped)
        return FALSE;
    AppleAicEventBase = (ULONGLONG)(ULONG_PTR)Mapped;
    AppleWdtPhase1MapRuntimeMmio();
    return TRUE;
}

static const HAL_ARM64_INTERRUPT_CONTROLLER AppleAicInterruptOps =
{
    "Apple AIC",
    AppleAicAcknowledgeInterrupt,
    AppleAicEndInterrupt,
    AppleAicEnableInterrupt,
    AppleAicDisableInterrupt,
    AppleAicSetInterruptPriority,
    AppleAicSetTriggerMode,
    AppleAicSetPriorityMask,
    AppleAicGetPriorityMask,
    AppleAicSendSgi,
    AppleAicInitializeProcessor,
    AppleAicPhase0Initialize,
    AppleAicPhase1MapRuntimeMmio,
    AIC_FIRST_DEVICE_INTID + AIC_MAX_DEVICE_IRQS,
    TRUE,
};

static HAL_ARM64_MSI_DOORBELL AppleAicPcieDoorbell;

static
VOID
AppleAicProbePcieMsi(
    _In_ PFDT Fdt,
    _In_ ULONG AicNode)
{
    static const PCSTR Compatibles[] = { "apple,t6020-pcie", "apple,pcie" };
    const UCHAR *Ranges;
    ULONG Node, Parent, Match, Length, Cells, Phandle;

    if (!HalpArm64FindDeviceTreeNode(Fdt, Compatibles, RTL_NUMBER_OF(Compatibles), &Node, &Parent, &Match) ||
        !FdtReadU32(Fdt, AicNode, "#interrupt-cells", &Cells) || Cells != 3 ||
        !FdtReadU32(Fdt, AicNode, "phandle", &Phandle))
        return;
    Ranges = FdtGetProperty(Fdt, Node, "msi-ranges", &Length);
    if (!Ranges || Length != (2 + Cells) * sizeof(ULONG) || FdtReadBigEndian32(Ranges) != Phandle ||
        FdtReadBigEndian32(Ranges + sizeof(ULONG)) != 0)
        return;
    AppleAicPcieDoorbell.Name = "Apple PCIe";
    AppleAicPcieDoorbell.Address = APPLE_PCIE_MSI_DOORBELL;
    AppleAicPcieDoorbell.FirstVector = AIC_FIRST_DEVICE_INTID + FdtReadBigEndian32(Ranges + 2 * sizeof(ULONG));
    AppleAicPcieDoorbell.VectorCount = FdtReadBigEndian32(Ranges + (1 + Cells) * sizeof(ULONG));
    if (AppleAicPcieDoorbell.FirstVector + AppleAicPcieDoorbell.VectorCount <= AIC_FIRST_DEVICE_INTID + AIC_MAX_DEVICE_IRQS)
        HalpArm64RegisterMsiDoorbell(&AppleAicPcieDoorbell);
}


BOOLEAN
AppleAicInterruptProbe(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    static const PCSTR Compatibles[] = { "apple,t8122-aic3", "apple,aic2" };
    static const ULONG IrqCfgOffsets[] = { AIC3_IRQ_CFG, AIC2_IRQ_CFG };
    FDT Fdt;
    ULONG Node;
    ULONG Parent;
    ULONG Match;
    ULONG IrqCfgOffset;
    ULONG Index;

    if (AppleAicDetected)
        return TRUE;
    if (!HalpArm64OpenDeviceTree(LoaderBlock, &Fdt) ||
        !HalpArm64FindDeviceTreeNode(&Fdt, Compatibles, RTL_NUMBER_OF(Compatibles), &Node, &Parent, &Match))
    {
        return FALSE;
    }
    IrqCfgOffset = IrqCfgOffsets[Match];
    if (!FdtReadReg(&Fdt, Node, Parent, 0, &AppleAicCorePhysical, &AppleAicCoreLength) ||
        !FdtReadReg(&Fdt, Node, Parent, 1, &AppleAicEventPhysical, &AppleAicEventLength) ||
        AppleAicCoreLength <= IrqCfgOffset || AppleAicEventLength < sizeof(ULONG))
    {
        DPRINT1("[AIC] interrupt controller node has no usable core and event registers\n");
        return FALSE;
    }

    AppleAicIrqCfgOffset = IrqCfgOffset;
    AppleAicCoreBase = AppleAicCorePhysical;
    AppleAicEventBase = AppleAicEventPhysical;
    AppleAicTimerPriority[0] = AIC_DEFAULT_PRIORITY;
    AppleAicTimerPriority[1] = AIC_DEFAULT_PRIORITY;
    for (Index = 0; Index < AIC_MAX_DEVICE_IRQS; Index++)
        AppleAicDevicePriority[Index] = AIC_DEFAULT_PRIORITY;
    for (Index = 0; Index < MAXIMUM_PROCESSORS; Index++)
        AppleAicMpidr[Index] = ARM64_READ_SYSREG(MPIDR_EL1);

    AppleAicDetected = TRUE;
    HalpArm64RegisterInterruptController(&AppleAicInterruptOps);
    DPRINT1("[AIC] Apple interrupt controller at %I64x (IRQ_CFG +0x%lx)\n", AppleAicCorePhysical, IrqCfgOffset);
    AppleWdtProbe(&Fdt);
    AppleAicProbePcieMsi(&Fdt, Node);
    return TRUE;
}
