/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ACPI sleep states S1-S3 for the AMD64 APIC HAL
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <hal.h>
#include <smp.h>
#include <drivers/acpi/acpipm.h>
#define NDEBUG
#include <debug.h>

#define HALP_SLEEP_TAG                  'pSlH'

#define ACPI_FADT_HW_REDUCED            (1UL << 20)
#define ACPI_FADT_LOW_POWER_S0_IDLE     (1UL << 21)

#define ACPI_PM1_SLP_TYP_SHIFT          10
#define ACPI_PM1_SLP_TYP_MASK           (7UL << ACPI_PM1_SLP_TYP_SHIFT)
#define ACPI_PM1_SLP_EN                 (1UL << 13)
#define ACPI_PM1_PWRBTN_STS             (1UL << 8)
#define ACPI_PM1_SLPBTN_STS             (1UL << 9)
#define ACPI_PM1_WAK_STS                (1UL << 15)

#define ACPI_SLEEP_CONTROL_TYP_SHIFT    2
#define ACPI_SLEEP_CONTROL_TYP_MASK     (7UL << ACPI_SLEEP_CONTROL_TYP_SHIFT)
#define ACPI_SLEEP_CONTROL_SLP_EN       (1UL << 5)
#define ACPI_SLEEP_STATUS_WAK_STS       (1UL << 7)

#define ACPI_GAS_SYSTEM_MEMORY          0
#define ACPI_GAS_SYSTEM_IO              1

#define MSR_TIME_STAMP_COUNTER          0x0010
#define MSR_TSC_ADJUST                  0x003B
#define MSR_MTRR_CAP                    0x00FE
#define MSR_MTRR_PHYS_BASE0             0x0200
#define MSR_MTRR_DEF_TYPE               0x02FF
#define MSR_XSS                         0x0DA0

#define MTRR_CAP_VCNT_MASK              0xFF
#define MTRR_CAP_FIX                    (1UL << 8)
#define MTRR_MAX_VARIABLE               16
#define MTRR_FIXED_COUNT                11

static const ULONG HalpFixedMtrrMsrs[MTRR_FIXED_COUNT] =
{
    0x250, 0x258, 0x259, 0x268, 0x269, 0x26A, 0x26B, 0x26C, 0x26D, 0x26E, 0x26F
};

typedef struct _HALP_ACPI_REGISTER
{
    UCHAR SpaceId;
    UCHAR Width;
    ULONG_PTR Address;
} HALP_ACPI_REGISTER, *PHALP_ACPI_REGISTER;

typedef struct _HALP_SLEEP_STATE
{
    SYSTEM_POWER_STATE SystemState;
    UCHAR SleepTypeA;
    UCHAR SleepTypeB;
} HALP_SLEEP_STATE, *PHALP_SLEEP_STATE;

typedef struct _HALP_SLEEP_PROCESSOR
{
    KPROCESSOR_STATE ProcessorState;
    ULONG64 XSaveMask;
    PVOID FpuArea;
    ULONG64 Xss;
    ULONG64 Efer;
    ULONG64 Pat;
    ULONG64 MtrrDefType;
    ULONG64 MtrrFixed[MTRR_FIXED_COUNT];
    ULONG64 MtrrVariable[2 * MTRR_MAX_VARIABLE];
    volatile LONG Resumed;
} HALP_SLEEP_PROCESSOR, *PHALP_SLEEP_PROCESSOR;

C_ASSERT(FIELD_OFFSET(HALP_SLEEP_PROCESSOR, XSaveMask) == sizeof(KPROCESSOR_STATE));
C_ASSERT(FIELD_OFFSET(HALP_SLEEP_PROCESSOR, FpuArea) == sizeof(KPROCESSOR_STATE) + sizeof(ULONG64));
C_ASSERT(FIELD_OFFSET(HALP_SLEEP_PROCESSOR, Xss) == sizeof(KPROCESSOR_STATE) + 2 * sizeof(ULONG64));

ULONG HalpSleepSaveContext(_Inout_ PHALP_SLEEP_PROCESSOR Save);
VOID HalpSleepResume64(VOID);
ULONG64 HalpSleepReadXcr0(VOID);

extern PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
extern PVOID HalpLowStub;

NTKERNELAPI
VOID
__cdecl
KeSaveStateForHibernate(
    _In_ PKPROCESSOR_STATE State);

static PM_DISPATCH_TABLE HalpPmDispatchTable = { ACPI_PM_DISPATCH_SIGNATURE, ACPI_PM_DISPATCH_VERSION, { NULL } };
static HALP_SLEEP_STATE HalpSleepStates[3];
static PHALP_SLEEP_PROCESSOR HalpSleepProcessors;
static ULONG HalpSleepProcessorCount;
static BOOLEAN HalpSleepInitialized;
static BOOLEAN HalpSleepHwReduced;
static BOOLEAN HalpSleepXSave;
static BOOLEAN HalpSleepXss;
static BOOLEAN HalpSleepMtrr;
static BOOLEAN HalpSleepTscAdjust;
static ULONG HalpSleepVariableMtrrs;
static BOOLEAN HalpSleepFixedMtrrs;
static PFACS HalpSleepFacs;
static HALP_ACPI_REGISTER HalpPm1aControl;
static HALP_ACPI_REGISTER HalpPm1bControl;
static HALP_ACPI_REGISTER HalpPm1aStatus;
static HALP_ACPI_REGISTER HalpPm1bStatus;
static HALP_ACPI_REGISTER HalpSleepControl;
static HALP_ACPI_REGISTER HalpSleepStatus;
static volatile LONG HalpSleepGeneration;
static volatile LONG64 HalpSleepTscOffset;
static ULONG64 HalpSleepTsc;

static
BOOLEAN
HalpSleepMapRegister(
    _Out_ PHALP_ACPI_REGISTER Register,
    _In_opt_ PGEN_ADDR Gas,
    _In_ ULONG LegacyPort,
    _In_ UCHAR Width)
{
    PHYSICAL_ADDRESS PhysicalAddress;

    RtlZeroMemory(Register, sizeof(*Register));
    Register->Width = Width;

    if (Gas && Gas->Address.QuadPart != 0)
    {
        if (Gas->AddressSpaceID == ACPI_GAS_SYSTEM_IO)
        {
            if (Gas->Address.QuadPart > MAXUSHORT)
                return FALSE;
            Register->SpaceId = ACPI_GAS_SYSTEM_IO;
            Register->Address = (ULONG_PTR)Gas->Address.QuadPart;
            return TRUE;
        }

        if (Gas->AddressSpaceID == ACPI_GAS_SYSTEM_MEMORY)
        {
            PhysicalAddress = Gas->Address;
            Register->SpaceId = ACPI_GAS_SYSTEM_MEMORY;
            Register->Address = (ULONG_PTR)MmMapIoSpace(PhysicalAddress, Width, MmNonCached);
            return Register->Address != 0;
        }

        return FALSE;
    }

    if (LegacyPort == 0 || LegacyPort > MAXUSHORT)
        return FALSE;

    Register->SpaceId = ACPI_GAS_SYSTEM_IO;
    Register->Address = LegacyPort;
    return TRUE;
}

static
ULONG
HalpSleepReadRegister(
    _In_ PHALP_ACPI_REGISTER Register)
{
    if (Register->SpaceId == ACPI_GAS_SYSTEM_IO)
    {
        switch (Register->Width)
        {
            case 1: return READ_PORT_UCHAR((PUCHAR)Register->Address);
            case 2: return READ_PORT_USHORT((PUSHORT)Register->Address);
            default: return READ_PORT_ULONG((PULONG)Register->Address);
        }
    }

    switch (Register->Width)
    {
        case 1: return READ_REGISTER_UCHAR((PUCHAR)Register->Address);
        case 2: return READ_REGISTER_USHORT((PUSHORT)Register->Address);
        default: return READ_REGISTER_ULONG((PULONG)Register->Address);
    }
}

static
VOID
HalpSleepWriteRegister(
    _In_ PHALP_ACPI_REGISTER Register,
    _In_ ULONG Value)
{
    if (Register->Address == 0)
        return;

    if (Register->SpaceId == ACPI_GAS_SYSTEM_IO)
    {
        switch (Register->Width)
        {
            case 1: WRITE_PORT_UCHAR((PUCHAR)Register->Address, (UCHAR)Value); break;
            case 2: WRITE_PORT_USHORT((PUSHORT)Register->Address, (USHORT)Value); break;
            default: WRITE_PORT_ULONG((PULONG)Register->Address, Value); break;
        }
        return;
    }

    switch (Register->Width)
    {
        case 1: WRITE_REGISTER_UCHAR((PUCHAR)Register->Address, (UCHAR)Value); break;
        case 2: WRITE_REGISTER_USHORT((PUSHORT)Register->Address, (USHORT)Value); break;
        default: WRITE_REGISTER_ULONG((PULONG)Register->Address, Value); break;
    }
}

static
BOOLEAN
HalpSleepMapPlatform(VOID)
{
    PFADT Fadt = &HalpFixedAcpiDescTable;
    PHYSICAL_ADDRESS FacsAddress;
    UCHAR StatusWidth;
    BOOLEAN HaveExtended;

    HaveExtended = Fadt->Header.Length >= RTL_SIZEOF_THROUGH_FIELD(FADT, x_gp1_blk);

    FacsAddress.QuadPart = Fadt->facs;
    if (HaveExtended && Fadt->x_firmware_ctrl.QuadPart != 0)
        FacsAddress = Fadt->x_firmware_ctrl;
    if (FacsAddress.QuadPart == 0)
    {
        DPRINT1("ACPI sleep: the FADT has no FACS\n");
        return FALSE;
    }

    HalpSleepFacs = MmMapIoSpace(FacsAddress, sizeof(FACS), MmCached);
    if (!HalpSleepFacs || HalpSleepFacs->Signature != FACS_SIGNATURE)
    {
        DPRINT1("ACPI sleep: no valid FACS at %I64x\n", FacsAddress.QuadPart);
        return FALSE;
    }

    if (HalpSleepHwReduced)
    {
        if (Fadt->Header.Length < RTL_SIZEOF_THROUGH_FIELD(FADT, sleep_status) ||
            !HalpSleepMapRegister(&HalpSleepControl, &Fadt->sleep_control, 0, 1) ||
            !HalpSleepMapRegister(&HalpSleepStatus, &Fadt->sleep_status, 0, 1))
        {
            DPRINT1("ACPI sleep: no sleep control and status registers\n");
            return FALSE;
        }
        return TRUE;
    }

    StatusWidth = Fadt->pm1_evt_len / 2;
    if (StatusWidth == 0 || StatusWidth > 4)
        StatusWidth = 2;

    if (!HalpSleepMapRegister(&HalpPm1aControl,
                              HaveExtended ? &Fadt->x_pm1a_ctrl_blk : NULL,
                              Fadt->pm1a_ctrl_blk_io_port,
                              2) ||
        !HalpSleepMapRegister(&HalpPm1aStatus,
                              HaveExtended ? &Fadt->x_pm1a_evt_blk : NULL,
                              Fadt->pm1a_evt_blk_io_port,
                              StatusWidth))
    {
        DPRINT1("ACPI sleep: no PM1a control or status register\n");
        return FALSE;
    }

    HalpSleepMapRegister(&HalpPm1bControl,
                         HaveExtended ? &Fadt->x_pm1b_ctrl_blk : NULL,
                         Fadt->pm1b_ctrl_blk_io_port,
                         2);
    HalpSleepMapRegister(&HalpPm1bStatus,
                         HaveExtended ? &Fadt->x_pm1b_evt_blk : NULL,
                         Fadt->pm1b_evt_blk_io_port,
                         StatusWidth);
    return TRUE;
}

static
VOID
HalpSleepDetectProcessorFeatures(VOID)
{
    INT CpuInfo[4];
    ULONG64 MtrrCap;

    __cpuid(CpuInfo, 0);
    if (CpuInfo[0] >= 7)
    {
        __cpuidex(CpuInfo, 7, 0);
        HalpSleepTscAdjust = (CpuInfo[1] & (1 << 1)) != 0;
    }

    HalpSleepXSave = (__readcr4() & CR4_XSAVE) != 0;
    if (HalpSleepXSave)
    {
        __cpuidex(CpuInfo, 0xD, 1);
        HalpSleepXss = (CpuInfo[0] & (1 << 3)) != 0;
    }

    __cpuid(CpuInfo, 1);
    HalpSleepMtrr = (CpuInfo[3] & (1 << 12)) != 0;
    if (HalpSleepMtrr)
    {
        MtrrCap = __readmsr(MSR_MTRR_CAP);
        HalpSleepVariableMtrrs = min((ULONG)(MtrrCap & MTRR_CAP_VCNT_MASK), MTRR_MAX_VARIABLE);
        HalpSleepFixedMtrrs = (MtrrCap & MTRR_CAP_FIX) != 0;
    }
}

static
ULONG
HalpSleepFpuAreaSize(VOID)
{
    INT CpuInfo[4];

    ULONG Size;

    if (!HalpSleepXSave)
        return 512;

    __cpuidex(CpuInfo, 0xD, 0);
    Size = max((ULONG)CpuInfo[2], 576UL);
    if (HalpSleepXss)
    {
        __cpuidex(CpuInfo, 0xD, 1);
        Size = max(Size, (ULONG)CpuInfo[1]);
    }
    return Size;
}

static
BOOLEAN
HalpSleepAllocateProcessors(VOID)
{
    ULONG Index;
    ULONG FpuSize;
    PUCHAR FpuArea;

    HalpSleepProcessorCount = (ULONG)KeNumberProcessors;
    HalpSleepProcessors = ExAllocatePoolZero(NonPagedPool,
                                             HalpSleepProcessorCount * sizeof(HALP_SLEEP_PROCESSOR),
                                             HALP_SLEEP_TAG);
    if (!HalpSleepProcessors)
        return FALSE;

    FpuSize = ALIGN_UP_BY(HalpSleepFpuAreaSize(), 64);
    FpuArea = ExAllocatePoolZero(NonPagedPool,
                                 HalpSleepProcessorCount * FpuSize + 64,
                                 HALP_SLEEP_TAG);
    if (!FpuArea)
        return FALSE;

    FpuArea = ALIGN_UP_POINTER_BY(FpuArea, 64);
    for (Index = 0; Index < HalpSleepProcessorCount; Index++)
        HalpSleepProcessors[Index].FpuArea = FpuArea + Index * FpuSize;

    return TRUE;
}

static
VOID
HalpSleepSaveMtrrs(
    _Out_ PHALP_SLEEP_PROCESSOR Save)
{
    ULONG Index;

    if (!HalpSleepMtrr)
        return;

    Save->MtrrDefType = __readmsr(MSR_MTRR_DEF_TYPE);
    if (HalpSleepFixedMtrrs)
    {
        for (Index = 0; Index < MTRR_FIXED_COUNT; Index++)
            Save->MtrrFixed[Index] = __readmsr(HalpFixedMtrrMsrs[Index]);
    }
    for (Index = 0; Index < 2 * HalpSleepVariableMtrrs; Index++)
        Save->MtrrVariable[Index] = __readmsr(MSR_MTRR_PHYS_BASE0 + Index);
}

static
BOOLEAN
HalpSleepMtrrsMatch(
    _In_ PHALP_SLEEP_PROCESSOR Save)
{
    ULONG Index;

    if (__readmsr(MSR_MTRR_DEF_TYPE) != Save->MtrrDefType)
        return FALSE;
    if (HalpSleepFixedMtrrs)
    {
        for (Index = 0; Index < MTRR_FIXED_COUNT; Index++)
        {
            if (__readmsr(HalpFixedMtrrMsrs[Index]) != Save->MtrrFixed[Index])
                return FALSE;
        }
    }
    for (Index = 0; Index < 2 * HalpSleepVariableMtrrs; Index++)
    {
        if (__readmsr(MSR_MTRR_PHYS_BASE0 + Index) != Save->MtrrVariable[Index])
            return FALSE;
    }
    return TRUE;
}

static
VOID
HalpSleepRestoreMtrrs(
    _In_ PHALP_SLEEP_PROCESSOR Save)
{
    ULONG_PTR Cr0, Cr4;
    ULONG Index;

    if (!HalpSleepMtrr || HalpSleepMtrrsMatch(Save))
        return;

    Cr4 = __readcr4();
    if (Cr4 & CR4_PGE)
        __writecr4(Cr4 & ~CR4_PGE);
    Cr0 = __readcr0();
    __writecr0((Cr0 | CR0_CD) & ~CR0_NW);
    __wbinvd();
    __writecr3(__readcr3());

    __writemsr(MSR_MTRR_DEF_TYPE, Save->MtrrDefType & ~0xC00ULL);
    if (HalpSleepFixedMtrrs)
    {
        for (Index = 0; Index < MTRR_FIXED_COUNT; Index++)
            __writemsr(HalpFixedMtrrMsrs[Index], Save->MtrrFixed[Index]);
    }
    for (Index = 0; Index < 2 * HalpSleepVariableMtrrs; Index++)
        __writemsr(MSR_MTRR_PHYS_BASE0 + Index, Save->MtrrVariable[Index]);
    __writemsr(MSR_MTRR_DEF_TYPE, Save->MtrrDefType);

    __wbinvd();
    __writecr3(__readcr3());
    __writecr0(Cr0);
    __writecr4(Cr4);
}

static
VOID
HalpSleepSaveProcessor(
    _Out_ PHALP_SLEEP_PROCESSOR Save)
{
    KeSaveStateForHibernate(&Save->ProcessorState);
    Save->Efer = __readmsr(MSR_EFER);
    Save->Pat = __readmsr(MSR_PAT);
    HalpSleepSaveMtrrs(Save);

    Save->XSaveMask = HalpSleepXSave ? HalpSleepReadXcr0() : 0;
    Save->Xss = HalpSleepXss ? __readmsr(MSR_XSS) : 0;
}

static
VOID
HalpSleepAdjustTsc(VOID)
{
    LONG64 Offset = HalpSleepTscOffset;

    if (Offset <= 0)
        return;

    if (HalpSleepTscAdjust)
        __writemsr(MSR_TSC_ADJUST, __readmsr(MSR_TSC_ADJUST) + Offset);
    else
        __writemsr(MSR_TIME_STAMP_COUNTER, __rdtsc() + Offset);
}

static
VOID
HalpSleepRestoreProcessor(
    _In_ ULONG Processor,
    _In_ PHALP_SLEEP_PROCESSOR Save)
{
    PKSPECIAL_REGISTERS Special = &Save->ProcessorState.SpecialRegisters;

    __writemsr(MSR_EFER, Save->Efer);
    HalpSleepRestoreMtrrs(Save);
    __writemsr(MSR_PAT, Save->Pat);
    __writemsr(MSR_STAR, Special->MsrStar);
    __writemsr(MSR_LSTAR, Special->MsrLStar);
    __writemsr(MSR_CSTAR, Special->MsrCStar);
    __writemsr(MSR_SYSCALL_MASK, Special->MsrSyscallMask);

    __writedr(0, Special->KernelDr0);
    __writedr(1, Special->KernelDr1);
    __writedr(2, Special->KernelDr2);
    __writedr(3, Special->KernelDr3);
    __writedr(6, Special->KernelDr6);
    __writedr(7, Special->KernelDr7);

    HalpSleepAdjustTsc();
    ApicInitializeLocalApic(Processor);
    __writecr8(Special->Cr8);
}

static
VOID
HalpSleepSetWakingVector(
    _In_ ULONG Vector)
{
    HalpSleepFacs->pFirmwareWakingVector = Vector;
    if (HalpSleepFacs->Length > 32)
        HalpSleepFacs->x_FirmwareWakingVector.QuadPart = 0;
}

static
BOOLEAN
HalpSleepWakeStatus(VOID)
{
    if (HalpSleepHwReduced)
        return (HalpSleepReadRegister(&HalpSleepStatus) & ACPI_SLEEP_STATUS_WAK_STS) != 0;

    return (HalpSleepReadRegister(&HalpPm1aStatus) & ACPI_PM1_WAK_STS) != 0;
}

static
VOID
HalpSleepClearWakeStatus(VOID)
{
    if (HalpSleepHwReduced)
    {
        HalpSleepWriteRegister(&HalpSleepStatus, ACPI_SLEEP_STATUS_WAK_STS);
        return;
    }

    HalpSleepWriteRegister(&HalpPm1aStatus, ACPI_PM1_WAK_STS | ACPI_PM1_PWRBTN_STS | ACPI_PM1_SLPBTN_STS);
    HalpSleepWriteRegister(&HalpPm1bStatus, ACPI_PM1_WAK_STS | ACPI_PM1_PWRBTN_STS | ACPI_PM1_SLPBTN_STS);
}

static
VOID
HalpSleepEnterState(
    _In_ PHALP_SLEEP_STATE State)
{
    ULONG Control;
    ULONG ControlA;
    ULONG ControlB = 0;

    if (HalpSleepHwReduced)
    {
        HalpSleepWriteRegister(&HalpSleepStatus, ACPI_SLEEP_STATUS_WAK_STS);
        Control = (State->SleepTypeA << ACPI_SLEEP_CONTROL_TYP_SHIFT) & ACPI_SLEEP_CONTROL_TYP_MASK;
        __wbinvd();
        HalpSleepWriteRegister(&HalpSleepControl, Control | ACPI_SLEEP_CONTROL_SLP_EN);
        return;
    }

    HalpSleepWriteRegister(&HalpPm1aStatus, ACPI_PM1_WAK_STS);
    HalpSleepWriteRegister(&HalpPm1bStatus, ACPI_PM1_WAK_STS);

    ControlA = HalpSleepReadRegister(&HalpPm1aControl) & ~(ACPI_PM1_SLP_TYP_MASK | ACPI_PM1_SLP_EN);
    ControlA |= (ULONG)State->SleepTypeA << ACPI_PM1_SLP_TYP_SHIFT;
    HalpSleepWriteRegister(&HalpPm1aControl, ControlA);
    if (HalpPm1bControl.Address)
    {
        ControlB = HalpSleepReadRegister(&HalpPm1bControl) & ~(ACPI_PM1_SLP_TYP_MASK | ACPI_PM1_SLP_EN);
        ControlB |= (ULONG)State->SleepTypeB << ACPI_PM1_SLP_TYP_SHIFT;
        HalpSleepWriteRegister(&HalpPm1bControl, ControlB);
    }

    __wbinvd();
    HalpSleepWriteRegister(&HalpPm1aControl, ControlA | ACPI_PM1_SLP_EN);
    if (HalpPm1bControl.Address)
        HalpSleepWriteRegister(&HalpPm1bControl, ControlB | ACPI_PM1_SLP_EN);
}

static
BOOLEAN
HalpSleepWaitForWake(
    _In_ ULONG Milliseconds)
{
    ULONG Elapsed;

    for (Elapsed = 0; Elapsed < Milliseconds; Elapsed++)
    {
        if (HalpSleepWakeStatus())
            return TRUE;
        KeStallExecutionProcessor(1000);
    }

    return HalpSleepWakeStatus();
}

static
VOID
HalpSleepRestartProcessors(VOID)
{
    PHALP_SLEEP_PROCESSOR Save;
    ULONG Processor;
    ULONG Wait;

    for (Processor = 1; Processor < HalpSleepProcessorCount; Processor++)
    {
        Save = &HalpSleepProcessors[Processor];
        Save->Resumed = 0;
        if (!HalpRestartProcessor(Processor, &Save->ProcessorState, HalpSleepResume64))
        {
            DPRINT1("Processor %lu could not be restarted after sleep\n", Processor);
            continue;
        }

        for (Wait = 0; Wait < 1000 && !Save->Resumed; Wait++)
            KeStallExecutionProcessor(1000);

        if (!Save->Resumed)
            DPRINT1("Processor %lu did not resume after sleep\n", Processor);
    }
}

static
NTSTATUS
NTAPI
HalpAcpiSleepHandler(
    _In_opt_ PVOID Context,
    _In_opt_ PENTER_STATE_SYSTEM_HANDLER SystemHandler,
    _In_opt_ PVOID SystemContext,
    _In_ LONG NumberProcessors,
    _In_opt_ LONG volatile *Number)
{
    PHALP_SLEEP_STATE State = Context;
    ULONG Processor = KeGetCurrentProcessorNumber();
    PHALP_SLEEP_PROCESSOR Save;
    ULONG_PTR Flags;
    LONG Generation;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Woke;

    UNREFERENCED_PARAMETER(SystemHandler);
    UNREFERENCED_PARAMETER(SystemContext);

    if (Processor >= HalpSleepProcessorCount || Number == NULL)
        return STATUS_INVALID_PARAMETER;

    Save = &HalpSleepProcessors[Processor];
    Generation = HalpSleepGeneration;
    Flags = __readeflags();
    _disable();

    if (State->SystemState == PowerSystemSleeping1)
    {
        if (Processor != 0)
        {
            InterlockedIncrement(Number);
            while (HalpSleepGeneration == Generation)
                YieldProcessor();
            __writeeflags(Flags);
            return STATUS_SUCCESS;
        }

        while (*Number != NumberProcessors - 1)
            YieldProcessor();

        HalpSleepEnterState(State);
        if (!HalpSleepWaitForWake(2000))
            Status = STATUS_UNSUCCESSFUL;
        HalpSleepClearWakeStatus();

        InterlockedIncrement(&HalpSleepGeneration);
        __writeeflags(Flags);
        return Status;
    }

    HalpSleepSaveProcessor(Save);
    if (HalpSleepSaveContext(Save) == 0)
    {
        if (Processor != 0)
        {
            InterlockedIncrement(Number);
            for (;;)
                __halt();
        }

        while (*Number != NumberProcessors - 1)
            YieldProcessor();

        HalpSaveInterruptControllers();
        if (!HalpPrepareLowStub(&Save->ProcessorState, HalpSleepResume64))
        {
            Status = STATUS_UNSUCCESSFUL;
            goto ResumeProcessors;
        }

        HalpSleepSetWakingVector(HalpLowStubPhysicalAddress.LowPart);
        HalpSleepTscOffset = 0;
        HalpSleepTsc = __rdtsc();

        HalpSleepEnterState(State);

        Woke = HalpSleepWaitForWake(2000);
        HalpSleepSetWakingVector(0);
        DPRINT1("The platform did not power down for S%u (wake status %u)\n",
                State->SystemState - PowerSystemWorking, Woke);
        Status = STATUS_UNSUCCESSFUL;
        goto ResumeProcessors;
    }

    if (Processor != 0)
    {
        HalpSleepRestoreProcessor(Processor, Save);
        InterlockedExchange(&Save->Resumed, 1);
        while (HalpSleepGeneration == Generation)
            YieldProcessor();
        __writeeflags(Flags);
        return STATUS_SUCCESS;
    }

    HalpSleepTscOffset = (LONG64)(HalpSleepTsc - __rdtsc());
    HalpSleepRestoreProcessor(0, Save);
    HalpSleepClearWakeStatus();
    HalpRestoreInterruptControllers();
    HalpRestoreClock();
    HalpSleepSetWakingVector(0);

ResumeProcessors:
    HalpSleepRestartProcessors();
    InterlockedIncrement(&HalpSleepGeneration);
    __writeeflags(Flags);
    return Status;
}

static
VOID
HalpSleepRegisterState(
    _In_ PACPI_PM_GET_SLEEP_TYPE GetSleepType,
    _In_ ULONG Index)
{
    POWER_STATE_HANDLER Handler;
    PHALP_SLEEP_STATE State = &HalpSleepStates[Index];
    NTSTATUS Status;

    State->SystemState = PowerSystemSleeping1 + Index;
    if (!NT_SUCCESS(GetSleepType(State->SystemState, &State->SleepTypeA, &State->SleepTypeB)))
        return;

    if (State->SystemState != PowerSystemSleeping1 && HalpLowStub == NULL)
    {
        DPRINT1("ACPI sleep: S%lu needs a low-memory wake stub\n", Index + 1);
        return;
    }

    RtlZeroMemory(&Handler, sizeof(Handler));
    Handler.Type = PowerStateSleeping1 + Index;
    Handler.Handler = HalpAcpiSleepHandler;
    Handler.Context = State;
    Status = ZwPowerInformation(SystemPowerStateHandler, &Handler, sizeof(Handler), NULL, 0);
    if (!NT_SUCCESS(Status))
        DPRINT1("ACPI sleep: registering S%lu failed: 0x%08lx\n", Index + 1, Status);
    else
        DPRINT1("ACPI sleep state S%lu available (SLP_TYP %u/%u)\n",
                Index + 1, State->SleepTypeA, State->SleepTypeB);
}

NTSTATUS
NTAPI
HalpInitPowerManagement(
    _In_ PPM_DISPATCH_TABLE PmDriverDispatchTable,
    _Out_ PPM_DISPATCH_TABLE *PmHalDispatchTable)
{
    PACPI_PM_GET_SLEEP_TYPE GetSleepType;
    ULONG Index;

    PAGED_CODE();

    if (!PmDriverDispatchTable || !PmHalDispatchTable ||
        PmDriverDispatchTable->Signature != ACPI_PM_DISPATCH_SIGNATURE ||
        PmDriverDispatchTable->Version < ACPI_PM_DISPATCH_VERSION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *PmHalDispatchTable = &HalpPmDispatchTable;
    if (HalpSleepInitialized)
        return STATUS_SUCCESS;
    HalpSleepInitialized = TRUE;

    GetSleepType = (PACPI_PM_GET_SLEEP_TYPE)PmDriverDispatchTable->Function[AcpiPmGetSleepType];
    if (!GetSleepType)
        return STATUS_INVALID_PARAMETER;

    if (HalpFixedAcpiDescTable.flags & ACPI_FADT_LOW_POWER_S0_IDLE)
    {
        DPRINT1("ACPI sleep: the platform is low-power S0 idle capable, S1-S3 are not offered\n");
        return STATUS_SUCCESS;
    }

    HalpSleepHwReduced = (HalpFixedAcpiDescTable.flags & ACPI_FADT_HW_REDUCED) != 0;
    if (!HalpSleepMapPlatform())
        return STATUS_NOT_SUPPORTED;

    HalpSleepDetectProcessorFeatures();
    if (!HalpSleepAllocateProcessors())
        return STATUS_INSUFFICIENT_RESOURCES;

    for (Index = 0; Index < RTL_NUMBER_OF(HalpSleepStates); Index++)
        HalpSleepRegisterState(GetSleepType, Index);

    return STATUS_SUCCESS;
}
