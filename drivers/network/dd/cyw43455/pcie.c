/*
 * PROJECT:     LiberNT Broadcom/Cypress CYW43xx Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     PCIe dongle bus: backplane access, firmware download and interrupts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"
#include "pcie.h"
#include <ntstrsafe.h>
#include <reactos/drivers/fdtdrv.h>

#define NDEBUG
#include <debug.h>

typedef struct _CYW_PCIE_CHIP_INFO
{
    USHORT ChipId;
    ULONG RamBase;
    ULONG OtpOffset;
    ULONG OtpWords;
} CYW_PCIE_CHIP_INFO;

typedef struct _CYW_PCIE_FIRMWARE_NAME
{
    USHORT ChipId;
    ULONG RevisionMask;
    PCSTR BaseName;
} CYW_PCIE_FIRMWARE_NAME;

static const CYW_PCIE_CHIP_INFO CywPcieChips[] =
{
    { 0x4377, 0x170000, 0x1120, 0x170 },
    { 0x4378, 0x352000, 0x1120, 0x170 },
    { 0x4387, 0x740000, 0x113C, 0x170 },
    { 0x4388, 0x200000, 0x115C, 0x150 },
};

static const CYW_PCIE_FIRMWARE_NAME CywPcieFirmwareNames[] =
{
    { 0x4377, 0xFFFFFFFF, "brcmfmac4377b3-pcie" },
    { 0x4378, 0x0000000F, "brcmfmac4378b1-pcie" },
    { 0x4378, 0xFFFFFFE0, "brcmfmac4378b3-pcie" },
    { 0x4387, 0xFFFFFFFF, "brcmfmac4387c2-pcie" },
    { 0x4388, 0x0000000F, "brcmfmac4388b0-pcie" },
    { 0x4388, 0xFFFFFFF0, "brcmfmac4388c0-pcie" },
};

static const USHORT CywPcieRestoredConfig[] =
{
    0x004, 0x04C, 0x058, 0x05C, 0x060, 0x064, 0x0DC, 0x228, 0x248, 0x4E0, 0x4F4
};

#define CYW_PCIE_BOARD_CANDIDATES 6
#define CYW_PCIE_BOARD_LENGTH     112

typedef struct _CYW_PCIE_BOARDS
{
    ULONG Count;
    CHAR Name[CYW_PCIE_BOARD_CANDIDATES][CYW_PCIE_BOARD_LENGTH];
} CYW_PCIE_BOARDS, *PCYW_PCIE_BOARDS;

static
ULONG
CywPcieRead32(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)(Pcie->Regs + Offset));
}

static
VOID
CywPcieWrite32(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)(Pcie->Regs + Offset), Value);
}

static
ULONG
CywPcieCoreRead32(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Register)
{
    return CywPcieRead32(Pcie, CYW_BAR0_PCIE2_REGS + Register);
}

static
VOID
CywPcieCoreWrite32(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    CywPcieWrite32(Pcie, CYW_BAR0_PCIE2_REGS + Register, Value);
}

static
ULONG
CywPcieIndirectRead(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Register)
{
    CywPcieCoreWrite32(Pcie, PCIE2_REG_CONFIGADDR, Register);
    return CywPcieCoreRead32(Pcie, PCIE2_REG_CONFIGDATA);
}

static
VOID
CywPcieIndirectWrite(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Register,
    _In_ ULONG Value)
{
    CywPcieCoreWrite32(Pcie, PCIE2_REG_CONFIGADDR, Register);
    CywPcieCoreWrite32(Pcie, PCIE2_REG_CONFIGDATA, Value);
}

BOOLEAN
CywPcieTcmRange(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Address,
    _In_ ULONG Length)
{
    return Pcie->Tcm != NULL && Address <= Pcie->TcmLength &&
           Length <= Pcie->TcmLength - Address;
}

ULONG
CywPcieReadTcm32(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Address)
{
    if (!CywPcieTcmRange(Pcie, Address, sizeof(ULONG)))
    {
        return MAXULONG;
    }
    return READ_REGISTER_ULONG((volatile ULONG *)(Pcie->Tcm + Address));
}

VOID
CywPcieWriteTcm32(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Address,
    _In_ ULONG Value)
{
    if (CywPcieTcmRange(Pcie, Address, sizeof(ULONG)))
    {
        WRITE_REGISTER_ULONG((volatile ULONG *)(Pcie->Tcm + Address), Value);
    }
}

USHORT
CywPcieReadTcm16(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Address)
{
    if (!CywPcieTcmRange(Pcie, Address, sizeof(USHORT)))
    {
        return MAXUSHORT;
    }
    return READ_REGISTER_USHORT((volatile USHORT *)(Pcie->Tcm + Address));
}

VOID
CywPcieWriteTcm16(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Address,
    _In_ USHORT Value)
{
    if (CywPcieTcmRange(Pcie, Address, sizeof(USHORT)))
    {
        WRITE_REGISTER_USHORT((volatile USHORT *)(Pcie->Tcm + Address), Value);
    }
}

static
VOID
CywPcieCopyToTcm(
    _In_ PCYW_PCIE Pcie,
    _In_ ULONG Address,
    _In_reads_bytes_opt_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    volatile ULONG *Target = (volatile ULONG *)(Pcie->Tcm + Address);
    ULONG Words = Length / sizeof(ULONG);
    ULONG Tail = Length % sizeof(ULONG);
    ULONG Index;
    ULONG Value;

    for (Index = 0; Index < Words; Index++)
    {
        if (Data != NULL)
        {
            RtlCopyMemory(&Value, Data + Index * sizeof(ULONG), sizeof(Value));
        }
        else
        {
            Value = 0;
        }
        WRITE_REGISTER_ULONG((volatile ULONG *)&Target[Index], Value);
    }
    if (Tail != 0)
    {
        Value = 0;
        if (Data != NULL)
        {
            RtlCopyMemory(&Value, Data + Words * sizeof(ULONG), Tail);
        }
        WRITE_REGISTER_ULONG((volatile ULONG *)&Target[Words], Value);
    }
}

static
NTSTATUS
CywPcieConfigRead(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Offset,
    _Out_ PULONG Value)
{
    *Value = MAXULONG;
    if (NdisMGetBusData(Adapter->MiniportAdapterHandle, PCI_WHICHSPACE_CONFIG,
                        Offset, Value, sizeof(ULONG)) != sizeof(ULONG))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywPcieConfigWrite(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    if (NdisMSetBusData(Adapter->MiniportAdapterHandle, PCI_WHICHSPACE_CONFIG,
                        Offset, &Value, sizeof(ULONG)) != sizeof(ULONG))
    {
        return STATUS_DEVICE_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywPcieSelectWindowLocked(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Base)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    ULONG Attempt;
    ULONG Readback;
    NTSTATUS Status;

    if (Pcie->Window == Base)
    {
        return STATUS_SUCCESS;
    }

    Pcie->Window = MAXULONG;
    for (Attempt = 0; Attempt < 2; Attempt++)
    {
        Status = CywPcieConfigWrite(Adapter, CYW_PCICFG_BAR0_WINDOW, Base);
        if (NT_SUCCESS(Status))
        {
            Status = CywPcieConfigRead(Adapter, CYW_PCICFG_BAR0_WINDOW, &Readback);
        }
        if (NT_SUCCESS(Status) && Readback == Base)
        {
            Pcie->Window = Base;
            return STATUS_SUCCESS;
        }
    }
    DPRINT1("CYW: the BAR0 window did not move to backplane 0x%08lx\n", Base);
    return STATUS_DEVICE_DATA_ERROR;
}

NTSTATUS
CywPcieBackplaneReadl(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _Out_ PULONG Value)
{
    NTSTATUS Status;

    *Value = MAXULONG;
    if (Adapter->Pcie == NULL || (Address & 3) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive, KernelMode, FALSE, NULL);
    Status = CywPcieSelectWindowLocked(Adapter, Address & ~(CYW_BAR0_WINDOW_LENGTH - 1));
    if (NT_SUCCESS(Status))
    {
        *Value = CywPcieRead32(Adapter->Pcie, Address & (CYW_BAR0_WINDOW_LENGTH - 1));
    }
    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    return Status;
}

NTSTATUS
CywPcieBackplaneWritel(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value)
{
    NTSTATUS Status;

    if (Adapter->Pcie == NULL || (Address & 3) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive, KernelMode, FALSE, NULL);
    Status = CywPcieSelectWindowLocked(Adapter, Address & ~(CYW_BAR0_WINDOW_LENGTH - 1));
    if (NT_SUCCESS(Status))
    {
        CywPcieWrite32(Adapter->Pcie, Address & (CYW_BAR0_WINDOW_LENGTH - 1), Value);
    }
    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    return Status;
}

static
NTSTATUS
CywPcieBackplaneRead16(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Address,
    _Out_writes_(Count) PUSHORT Values,
    _In_ ULONG Count)
{
    ULONG Offset = Address & (CYW_BAR0_WINDOW_LENGTH - 1);
    ULONG Index;
    NTSTATUS Status;

    if (Offset + Count * sizeof(USHORT) > CYW_BAR0_WINDOW_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }
    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive, KernelMode, FALSE, NULL);
    Status = CywPcieSelectWindowLocked(Adapter, Address & ~(CYW_BAR0_WINDOW_LENGTH - 1));
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Count; Index++)
        {
            Values[Index] = READ_REGISTER_USHORT(
                (volatile USHORT *)(Adapter->Pcie->Regs + Offset + Index * sizeof(USHORT)));
        }
    }
    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    return Status;
}

static
PCYW_CORE
CywPcieFindCore(
    _In_ PCYW_PCIE Pcie,
    _In_ USHORT Id,
    _In_ ULONG Instance)
{
    ULONG Index;

    for (Index = 0; Index < Pcie->CoreCount; Index++)
    {
        if (Pcie->Cores[Index].Id == Id)
        {
            if (Instance == 0)
            {
                return &Pcie->Cores[Index];
            }
            Instance--;
        }
    }
    return NULL;
}

static
NTSTATUS
CywPcieScanCores(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    ULONG Remaining = 4096;
    ULONG EromAddress;
    ULONG Descriptor;
    NTSTATUS Status;

    Pcie->CoreCount = 0;
    Status = CywPcieBackplaneReadl(Adapter, SI_ENUM_BASE_DEFAULT + CC_EROMPTR, &EromAddress);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (EromAddress == 0 || EromAddress == MAXULONG || (EromAddress & 3) != 0)
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    while (Remaining != 0)
    {
        ULONG Type;
        ULONG Id;
        ULONG Info;
        ULONG Wrappers;
        ULONG Base;
        ULONG Wrap;

        Status = CywEromReadDescriptor(Adapter, &EromAddress, &Remaining, &Descriptor);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Type = CywEromDescriptorType(Descriptor);
        if (Type == DMP_DESC_EOT)
        {
            break;
        }
        if (!(Descriptor & DMP_DESC_VALID) || Type != DMP_DESC_COMPONENT)
        {
            continue;
        }

        Id = (Descriptor & DMP_COMP_PARTNUM) >> DMP_COMP_PARTNUM_S;
        Status = CywEromReadDescriptor(Adapter, &EromAddress, &Remaining, &Info);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if ((Info & DMP_DESC_TYPE_MSK) != DMP_DESC_COMPONENT)
        {
            return STATUS_DEVICE_DATA_ERROR;
        }
        Wrappers = ((Info & DMP_COMP_NUM_MWRAP) >> DMP_COMP_NUM_MWRAP_S) +
                   ((Info & DMP_COMP_NUM_SWRAP) >> DMP_COMP_NUM_SWRAP_S);
        if (Wrappers == 0 && Id != BCMA_CORE_GCI)
        {
            continue;
        }

        Status = CywEromGetCoreBases(Adapter, &EromAddress, &Remaining, &Base, &Wrap);
        if (!NT_SUCCESS(Status) || Base == 0)
        {
            continue;
        }
        if (Pcie->CoreCount == CYW_MAX_CORES)
        {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Pcie->Cores[Pcie->CoreCount].Id = (USHORT)Id;
        Pcie->Cores[Pcie->CoreCount].Rev = (UCHAR)((Info & DMP_COMP_REVISION) >> DMP_COMP_REVISION_S);
        Pcie->Cores[Pcie->CoreCount].Base = Base;
        Pcie->Cores[Pcie->CoreCount].Wrap = Wrap;
        Pcie->CoreCount++;
    }

    Pcie->Chipcommon = CywPcieFindCore(Pcie, BCMA_CORE_CHIPCOMMON, 0);
    Pcie->Pcie2 = CywPcieFindCore(Pcie, BCMA_CORE_PCIE2, 0);
    Pcie->Gci = CywPcieFindCore(Pcie, BCMA_CORE_GCI, 0);
    Pcie->Sysmem = CywPcieFindCore(Pcie, BCMA_CORE_SYS_MEM, 0);
    Pcie->Cpu = CywPcieFindCore(Pcie, BCMA_CORE_ARM_CR4, 0);
    Adapter->CpuCoreType = CywCpuCoreArmCr4;
    if (Pcie->Cpu == NULL)
    {
        Pcie->Cpu = CywPcieFindCore(Pcie, BCMA_CORE_ARM_CA7, 0);
        Adapter->CpuCoreType = CywCpuCoreArmCa7;
    }
    if (Pcie->Chipcommon == NULL || Pcie->Pcie2 == NULL || Pcie->Cpu == NULL ||
        Pcie->Cpu->Wrap == 0 || (Adapter->CpuCoreType == CywCpuCoreArmCa7 && Pcie->Sysmem == NULL))
    {
        DPRINT1("CYW: the backplane lacks a core the PCIe dongle needs (%lu cores found)\n",
                Pcie->CoreCount);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywPcieSetPassive(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    PCYW_CORE D11;
    ULONG Instance;
    ULONG Ioctl;
    NTSTATUS Status;

    Status = CywBackplaneReadl(Adapter, Pcie->Cpu->Wrap + BCMA_IOCTL, &Ioctl);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = CywChipResetCore(Adapter, Pcie->Cpu->Wrap, Ioctl & ARMCR4_BCMA_IOCTL_CPUHALT,
                              ARMCR4_BCMA_IOCTL_CPUHALT, ARMCR4_BCMA_IOCTL_CPUHALT);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (Instance = 0; (D11 = CywPcieFindCore(Pcie, BCMA_CORE_80211, Instance)) != NULL; Instance++)
    {
        if (D11->Wrap == 0)
        {
            continue;
        }
        Status = CywChipDisableCore(Adapter, D11->Wrap,
                                    D11_BCMA_IOCTL_PHYRESET | D11_BCMA_IOCTL_PHYCLOCKEN,
                                    D11_BCMA_IOCTL_PHYCLOCKEN);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywPcieWatchdogReset(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    ULONG Cc = Pcie->Chipcommon->Base;
    LARGE_INTEGER Delay;
    ULONG LinkControl = 0;
    ULONG Value;
    ULONG Index;
    NTSTATUS Status;

    Status = CywPcieConfigRead(Adapter, CYW_PCICFG_LINK_STATUS_CTRL, &LinkControl);
    if (NT_SUCCESS(Status))
    {
        Status = CywPcieConfigWrite(Adapter, CYW_PCICFG_LINK_STATUS_CTRL,
                                    LinkControl & ~CYW_PCICFG_ASPM_ENABLE);
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Pcie->Chipcommon->Rev >= 65)
    {
        ULONG Mask = CC_WD_SSRESET_PCIE_F0_EN;

        if (Pcie->Pcie2->Rev < 66)
        {
            Mask |= CC_WD_SSRESET_PCIE_ALL_FN_EN;
        }
        Status = CywBackplaneReadl(Adapter, Cc + CC_REG_WATCHDOG, &Value);
        if (NT_SUCCESS(Status))
        {
            Value = (Value & ~CC_WD_ENABLE_MASK) | Mask;
            Status = CywBackplaneWritel(Adapter, Cc + CC_REG_WATCHDOG, Value);
        }
        if (NT_SUCCESS(Status))
        {
            Value = (Value & ~CC_WD_COUNTER_MASK) | 4;
            Status = CywBackplaneWritel(Adapter, Cc + CC_REG_WATCHDOG, Value);
        }
        Delay.QuadPart = -10LL * 10000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        Pcie->Window = MAXULONG;
        if (NT_SUCCESS(Status))
        {
            Status = CywBackplaneReadl(Adapter, Cc + CC_REG_INTSTATUS, &Value);
        }
        if (NT_SUCCESS(Status))
        {
            Status = CywBackplaneWritel(Adapter, Cc + CC_REG_INTSTATUS, Value | Mask);
        }
    }
    else
    {
        Status = CywBackplaneWritel(Adapter, Cc + CC_REG_WATCHDOG, 4);
        Delay.QuadPart = -100LL * 10000LL;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        Pcie->Window = MAXULONG;
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CywPcieConfigWrite(Adapter, CYW_PCICFG_LINK_STATUS_CTRL, LinkControl);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    KeWaitForSingleObject(&Adapter->BackplaneLock, Executive, KernelMode, FALSE, NULL);
    Status = CywPcieSelectWindowLocked(Adapter, Pcie->Pcie2->Base);
    KeReleaseMutex(&Adapter->BackplaneLock, FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Pcie->Pcie2->Rev <= 13)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(CywPcieRestoredConfig); Index++)
        {
            CywPcieIndirectWrite(Pcie, CywPcieRestoredConfig[Index],
                                 CywPcieIndirectRead(Pcie, CywPcieRestoredConfig[Index]));
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywPcieMeasureRam(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    ULONG Size = 0;
    ULONG Index;
    ULONG Value;
    NTSTATUS Status;

    if (Adapter->CpuCoreType == CywCpuCoreArmCr4)
    {
        ULONG Capability;
        ULONG Banks;

        Status = CywBackplaneReadl(Adapter, Pcie->Cpu->Base + ARMCR4_REG_CAP, &Capability);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        Banks = (Capability & ARMCR4_CAP_TCBANB_MASK) +
                ((Capability & ARMCR4_CAP_TCBBNB_MASK) >> ARMCR4_CAP_TCBBNB_SHIFT);
        for (Index = 0; Index < Banks; Index++)
        {
            ULONG Unit = ARMCR4_BANK_SIZE_UNIT;

            Status = CywBackplaneWritel(Adapter, Pcie->Cpu->Base + ARMCR4_REG_BANKIDX, Index);
            if (NT_SUCCESS(Status))
            {
                Status = CywBackplaneReadl(Adapter, Pcie->Cpu->Base + ARMCR4_REG_BANKINFO, &Value);
            }
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            if (Value & ARMCR4_BANK_1K_UNITS)
            {
                Unit /= 8;
            }
            Size += ((Value & ARMCR4_BANK_SIZE_MASK) + 1) * Unit;
        }
    }
    else
    {
        BOOLEAN IsUp;
        ULONG CoreInfo;
        ULONG RomBanks;
        ULONG RamBanks;

        Status = CywChipIsCoreUp(Adapter, Pcie->Sysmem->Wrap, &IsUp);
        if (NT_SUCCESS(Status) && !IsUp)
        {
            Status = CywChipResetCore(Adapter, Pcie->Sysmem->Wrap, 0, 0, 0);
        }
        if (NT_SUCCESS(Status))
        {
            Status = CywBackplaneReadl(Adapter, Pcie->Sysmem->Base + SOCRAM_COREINFO, &CoreInfo);
        }
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
        if (Pcie->Sysmem->Rev >= SYSMEM_NEW_COREINFO_FIRST_REV)
        {
            RomBanks = (CoreInfo & SYSMEM_COREINFO_NEW_ROMNB_MASK) >> SYSMEM_COREINFO_NEW_ROMNB_SHIFT;
            RamBanks = (CoreInfo & SYSMEM_COREINFO_NEW_SRNB_MASK) >> SYSMEM_COREINFO_NEW_SRNB_SHIFT;
        }
        else
        {
            RomBanks = (CoreInfo & SYSMEM_COREINFO_ROMNB_MASK) >> SYSMEM_COREINFO_ROMNB_SHIFT;
            RamBanks = CoreInfo & SYSMEM_COREINFO_SRNB_MASK;
        }
        for (Index = 0; Index < RamBanks; Index++)
        {
            Status = CywBackplaneWritel(Adapter, Pcie->Sysmem->Base + SOCRAM_BANKIDX,
                                        (SOCRAM_MEMTYPE_RAM << SOCRAM_BANKIDX_MEMTYPE_SHIFT) |
                                        (Index + RomBanks));
            if (NT_SUCCESS(Status))
            {
                Status = CywBackplaneReadl(Adapter, Pcie->Sysmem->Base + SOCRAM_BANKINFO, &Value);
            }
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            Size += ((Value & SOCRAM_BANKINFO_SZMASK) + 1) * SOCRAM_BANKINFO_SZBASE;
        }
    }

    if (Size == 0 || Size > CYW_CHIP_MAX_MEMSIZE)
    {
        DPRINT1("CYW: the dongle reports an unusable RAM size of 0x%lx\n", Size);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Adapter->RamSize = Size;
    return STATUS_SUCCESS;
}

static
VOID
CywPcieCopyParam(
    _Out_writes_(CYW_OTP_PARAM_LENGTH) PCHAR Target,
    _In_reads_(Length) const CHAR *Source,
    _In_ ULONG Length)
{
    if (Length >= CYW_OTP_PARAM_LENGTH)
    {
        Length = CYW_OTP_PARAM_LENGTH - 1;
    }
    RtlCopyMemory(Target, Source, Length);
    Target[Length] = ANSI_NULL;
}

static
BOOLEAN
CywPcieParseOtpVendor(
    _In_ PCYW_PCIE Pcie,
    _In_reads_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Header;
    ULONG Position;

    if (Length < 6)
    {
        return FALSE;
    }
    RtlCopyMemory(&Header, Data, sizeof(Header));
    if (Header != CYW_OTP_VENDOR_HEADER)
    {
        return FALSE;
    }

    Position = 4;
    while (Position < Length && Data[Position] != 0)
    {
        Position++;
    }
    Position++;

    while (Position < Length && Data[Position] != 0)
    {
        CHAR Tag;
        ULONG Start;

        if (Data[Position] == ' ')
        {
            Position++;
            continue;
        }
        if (Position + 1 >= Length || Data[Position + 1] != '=')
        {
            return FALSE;
        }
        Tag = (CHAR)Data[Position];
        Position += 2;
        Start = Position;
        while (Position < Length && Data[Position] != 0 && Data[Position] != ' ')
        {
            Position++;
        }
        if (Position - Start >= CYW_OTP_PARAM_LENGTH)
        {
            return FALSE;
        }
        if (Tag == 'M')
        {
            CywPcieCopyParam(Pcie->OtpModule, (const CHAR *)Data + Start, Position - Start);
        }
        else if (Tag == 'V')
        {
            CywPcieCopyParam(Pcie->OtpVendor, (const CHAR *)Data + Start, Position - Start);
        }
        else if (Tag == 'm')
        {
            CywPcieCopyParam(Pcie->OtpVersion, (const CHAR *)Data + Start, Position - Start);
        }
    }

    return Pcie->OtpModule[0] != ANSI_NULL && Pcie->OtpVendor[0] != ANSI_NULL &&
           Pcie->OtpVersion[0] != ANSI_NULL;
}

static
VOID
CywPcieReadOtp(
    _In_ PCYW_ADAPTER Adapter,
    _In_ const CYW_PCIE_CHIP_INFO *Chip)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    PUSHORT Words;
    PUCHAR Bytes;
    ULONG Length;
    ULONG Position = 0;
    NTSTATUS Status;

    if (Pcie->Gci == NULL)
    {
        DPRINT1("CYW: no GCI core, the OTP board parameters cannot be read\n");
        return;
    }
    Words = CywAllocate(Chip->OtpWords * sizeof(USHORT));
    if (Words == NULL)
    {
        return;
    }
    Status = CywPcieBackplaneRead16(Adapter, Pcie->Gci->Base + Chip->OtpOffset, Words, Chip->OtpWords);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CYW: reading the OTP failed (0x%08lx)\n", Status);
        CywFree(Words);
        return;
    }

    Bytes = (PUCHAR)Words;
    Length = Chip->OtpWords * sizeof(USHORT);
    while (Position + 1 < Length && Bytes[Position] != 0)
    {
        UCHAR Type = Bytes[Position];
        UCHAR Size = Bytes[Position + 1];

        if (Position + 2 + Size > Length)
        {
            break;
        }
        if (Type == CYW_OTP_SYS_VENDOR &&
            CywPcieParseOtpVendor(Pcie, Bytes + Position + 2, Size))
        {
            Pcie->OtpValid = TRUE;
        }
        Position += 2 + Size;
    }
    CywFree(Words);

    if (Pcie->OtpValid)
    {
        DPRINT1("CYW: OTP module %s vendor %s version %s\n",
                Pcie->OtpModule, Pcie->OtpVendor, Pcie->OtpVersion);
    }
    else
    {
        DPRINT1("CYW: the OTP has no usable module/vendor/version record\n");
    }
}

static
VOID
CywPcieCopyString(
    _Out_writes_(Capacity) PCHAR Target,
    _In_ ULONG Capacity,
    _In_reads_bytes_opt_(Length) const VOID *Source,
    _In_ ULONG Length)
{
    ULONG Count = 0;
    const CHAR *Text = Source;

    if (Source != NULL)
    {
        while (Count < Length && Count + 1 < Capacity && Text[Count] != ANSI_NULL)
        {
            Target[Count] = Text[Count];
            Count++;
        }
    }
    Target[Count] = ANSI_NULL;
}

static
VOID
CywPcieReadPlatform(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    FDT_DRIVER_TREE Tree;
    const VOID *Value;
    ULONG Length;

    if (!NT_SUCCESS(FdtDriverOpenAlias("wifi0", &Tree)))
    {
        DPRINT1("CYW: the firmware tree describes no wifi0 node\n");
        return;
    }

    Value = FdtGetProperty(&Tree.Fdt, Tree.Node, "brcm,board-type", &Length);
    CywPcieCopyString(Pcie->BoardType, sizeof(Pcie->BoardType), Value, Length);
    Value = FdtGetProperty(&Tree.Fdt, Tree.Node, "apple,antenna-sku", &Length);
    CywPcieCopyString(Pcie->AntennaSku, sizeof(Pcie->AntennaSku), Value, Length);
    Value = FdtGetProperty(&Tree.Fdt, Tree.Node, "local-mac-address", &Length);
    if (Value != NULL && Length == sizeof(Pcie->MacAddress))
    {
        const UCHAR *Mac = Value;

        RtlCopyMemory(Pcie->MacAddress, Mac, sizeof(Pcie->MacAddress));
        Pcie->HaveMacAddress = (Mac[0] & 1) == 0 &&
                               (Mac[0] | Mac[1] | Mac[2] | Mac[3] | Mac[4] | Mac[5]) != 0;
    }
    FdtDriverClose(&Tree);

    DPRINT1("CYW: board type '%s', antenna SKU '%s', %s MAC address\n",
            Pcie->BoardType, Pcie->AntennaSku,
            Pcie->HaveMacAddress ? "platform" : "no platform");
}

static
VOID
CywPcieBuildBoards(
    _In_ PCYW_PCIE Pcie,
    _Out_ PCYW_PCIE_BOARDS Boards)
{
    PCSTR B = Pcie->BoardType;
    ULONG N = 0;

    RtlZeroMemory(Boards, sizeof(*Boards));
    if (B[0] == ANSI_NULL)
    {
        return;
    }
    if (Pcie->OtpValid && Pcie->AntennaSku[0] != ANSI_NULL)
    {
        RtlStringCbPrintfA(Boards->Name[N++], CYW_PCIE_BOARD_LENGTH, "%s-%s-%s-%s-%s", B,
                           Pcie->OtpModule, Pcie->OtpVendor, Pcie->OtpVersion, Pcie->AntennaSku);
        RtlStringCbPrintfA(Boards->Name[N++], CYW_PCIE_BOARD_LENGTH, "%s-%s-%s-%s", B,
                           Pcie->OtpModule, Pcie->OtpVendor, Pcie->OtpVersion);
        RtlStringCbPrintfA(Boards->Name[N++], CYW_PCIE_BOARD_LENGTH, "%s-%s-%s", B,
                           Pcie->OtpModule, Pcie->OtpVendor);
        RtlStringCbPrintfA(Boards->Name[N++], CYW_PCIE_BOARD_LENGTH, "%s-%s", B, Pcie->OtpModule);
        RtlStringCbPrintfA(Boards->Name[N++], CYW_PCIE_BOARD_LENGTH, "%s-%s", B, Pcie->AntennaSku);
    }
    RtlStringCbCopyA(Boards->Name[N++], CYW_PCIE_BOARD_LENGTH, B);
    Boards->Count = N;
}

static
NTSTATUS
CywPcieLoadFile(
    _In_ PCSTR BaseName,
    _In_ PCYW_PCIE_BOARDS Boards,
    _In_z_ PCSTR Suffix,
    _Outptr_opt_ PUCHAR *Data,
    _Out_opt_ PULONG Size,
    _Outptr_opt_ PWSTR *Path)
{
    WCHAR Candidate[260];
    ULONG Index;
    NTSTATUS Status = STATUS_OBJECT_NAME_NOT_FOUND;

    for (Index = 0; Index <= Boards->Count; Index++)
    {
        PUCHAR Buffer = NULL;
        ULONG Length = 0;

        if (Index < Boards->Count)
        {
            Status = RtlStringCbPrintfW(Candidate, sizeof(Candidate), CYW_FW_DIR L"%hs.%hs%hs",
                                        BaseName, Boards->Name[Index], Suffix);
        }
        else
        {
            Status = RtlStringCbPrintfW(Candidate, sizeof(Candidate), CYW_FW_DIR L"%hs%hs",
                                        BaseName, Suffix);
        }
        if (!NT_SUCCESS(Status))
        {
            continue;
        }

        Status = CywReadFile(Candidate, &Buffer, &Length);
        if (!NT_SUCCESS(Status))
        {
            continue;
        }
        DPRINT1("CYW: using %S\n", Candidate);
        if (Data != NULL)
        {
            *Data = Buffer;
            *Size = Length;
        }
        else
        {
            CywFree(Buffer);
        }
        if (Path != NULL)
        {
            SIZE_T Bytes = (wcslen(Candidate) + 1) * sizeof(WCHAR);

            *Path = CywAllocate((ULONG)Bytes);
            if (*Path == NULL)
            {
                if (Data != NULL)
                {
                    CywFree(*Data);
                    *Data = NULL;
                }
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            RtlCopyMemory(*Path, Candidate, Bytes);
        }
        return STATUS_SUCCESS;
    }
    return Status;
}

static
NTSTATUS
CywPcieAddFooter(
    _In_ PCYW_PCIE Pcie,
    _Inout_ PULONG Address,
    _In_ ULONG Magic,
    _In_ ULONG Length,
    _In_ ULONG Floor)
{
    CYW_FW_FOOTER Footer;
    ULONG Start;

    Length = ALIGN_UP_BY(Length, sizeof(ULONG));
    Start = *Address - Length - sizeof(Footer);
    if (Length > MAXUSHORT || Start > *Address || Start < Floor)
    {
        DPRINT1("CYW: no room for footer 0x%08lx of 0x%lx bytes\n", Magic, Length);
        return STATUS_BUFFER_TOO_SMALL;
    }
    Footer.Magic = Magic;
    Footer.Length = (Magic == CYW_RANDOM_SEED_MAGIC) ? Length : (Length | ((Length ^ MAXUSHORT) << 16));
    CywPcieCopyToTcm(Pcie, *Address - sizeof(Footer), (const UCHAR *)&Footer, sizeof(Footer));
    *Address = Start;
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywPcieAddFooters(
    _In_ PCYW_ADAPTER Adapter,
    _Inout_ PULONG Address,
    _In_reads_bytes_opt_(SignatureSize) const UCHAR *Signature,
    _In_ ULONG SignatureSize)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    ULONG HeapStart = ALIGN_UP_BY(Adapter->RamBase + Pcie->FirmwareSize + CYW_FOOTER_HEAP_GAP, sizeof(ULONG));
    ULONG Floor = HeapStart + CYW_FOOTER_HEAP_SIZE;
    UCHAR Seed[CYW_RANDOM_SEED_LENGTH];
    CYW_FW_MEMMAP Map;
    LARGE_INTEGER Counter;
    ULONG RandomState;
    ULONG Index;
    NTSTATUS Status;

    Status = CywPcieAddFooter(Pcie, Address, CYW_RANDOM_SEED_MAGIC, sizeof(Seed), Floor);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Counter = KeQueryPerformanceCounter(NULL);
    RandomState = Counter.LowPart ^ Counter.HighPart ^ (ULONG)KeQueryInterruptTime();
    if (RandomState == 0)
    {
        RandomState = 0xA5A5A5A5;
    }
    for (Index = 0; Index < sizeof(Seed); Index++)
    {
        RandomState ^= RandomState << 13;
        RandomState ^= RandomState >> 17;
        RandomState ^= RandomState << 5;
        Seed[Index] = (UCHAR)(RandomState >> 24);
    }
    CywPcieCopyToTcm(Pcie, *Address, Seed, sizeof(Seed));

    if (Signature == NULL)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&Map, sizeof(Map));
    Map.Signature.End = *Address;
    Status = CywPcieAddFooter(Pcie, Address, CYW_FW_SIG_MAGIC, SignatureSize, Floor);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Map.Signature.Start = *Address;
    Map.VerificationStatus.End = *Address;
    Status = CywPcieAddFooter(Pcie, Address, CYW_VSTATUS_MAGIC, CYW_VSTATUS_LENGTH, Floor);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Map.VerificationStatus.Start = *Address;
    Status = CywPcieAddFooter(Pcie, Address, CYW_MEMMAP_MAGIC, sizeof(Map), Floor);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Map.Firmware.Start = Adapter->RamBase;
    Map.Firmware.End = Adapter->RamBase + Pcie->FirmwareSize;
    Map.Heap.Start = HeapStart;
    Map.Heap.End = HeapStart + CYW_FOOTER_HEAP_SIZE;
    if (Map.Heap.End > *Address)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    CywPcieCopyToTcm(Pcie, Map.Signature.Start, Signature, SignatureSize);
    CywPcieCopyToTcm(Pcie, Map.VerificationStatus.Start, NULL, CYW_VSTATUS_LENGTH);
    CywPcieCopyToTcm(Pcie, *Address, (const UCHAR *)&Map, sizeof(Map));
    Status = CywPcieAddFooter(Pcie, Address, CYW_END_MAGIC, 0, Floor);
    if (NT_SUCCESS(Status))
    {
        Pcie->SkipResetVector = TRUE;
    }
    return Status;
}

static
NTSTATUS
CywPcieReadShared(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Shared)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    ULONG Console;
    ULONG HostCap;

    Pcie->SharedAddress = Shared;
    Pcie->SharedFlags = CywPcieReadTcm32(Pcie, Shared + CYW_SHARED_FLAGS);
    Pcie->SharedVersion = (UCHAR)(Pcie->SharedFlags & CYW_SHARED_VERSION_MASK);
    if (Pcie->SharedVersion < CYW_SHARED_MIN_VERSION || Pcie->SharedVersion > CYW_SHARED_MAX_VERSION)
    {
        DPRINT1("CYW: the firmware speaks PCIe protocol version %u, which is not supported\n",
                Pcie->SharedVersion);
        return STATUS_NOT_SUPPORTED;
    }

    Pcie->IndexSize = 0;
    if (Pcie->SharedFlags & CYW_SHARED_DMA_INDEX)
    {
        Pcie->IndexSize = (Pcie->SharedFlags & CYW_SHARED_DMA_2B_IDX) ? sizeof(USHORT) : sizeof(ULONG);
    }
    Pcie->MaxRxBufPost = CywPcieReadTcm16(Pcie, Shared + CYW_SHARED_MAX_RXBUFPOST);
    if (Pcie->MaxRxBufPost == 0)
    {
        Pcie->MaxRxBufPost = CYW_DEFAULT_MAX_RXBUFPOST;
    }
    Pcie->RxDataOffset = CywPcieReadTcm32(Pcie, Shared + CYW_SHARED_RX_DATAOFFSET);
    Pcie->HtodMailboxAddress = CywPcieReadTcm32(Pcie, Shared + CYW_SHARED_HTOD_MB_DATA_ADDR);
    Pcie->DtohMailboxAddress = CywPcieReadTcm32(Pcie, Shared + CYW_SHARED_DTOH_MB_DATA_ADDR);
    Pcie->RingInfoAddress = CywPcieReadTcm32(Pcie, Shared + CYW_SHARED_RING_INFO_ADDR);

    Console = CywPcieReadTcm32(Pcie, Shared + CYW_SHARED_CONSOLE_ADDR);
    if (CywPcieTcmRange(Pcie, Console, CYW_CONSOLE_WRITEIDX + sizeof(ULONG)))
    {
        Pcie->ConsoleIndexAddress = Console + CYW_CONSOLE_WRITEIDX;
        Pcie->ConsoleBuffer = CywPcieReadTcm32(Pcie, Console + CYW_CONSOLE_BUFADDR);
        Pcie->ConsoleSize = CywPcieReadTcm32(Pcie, Console + CYW_CONSOLE_BUFSIZE);
        if (!CywPcieTcmRange(Pcie, Pcie->ConsoleBuffer, Pcie->ConsoleSize))
        {
            Pcie->ConsoleSize = 0;
        }
    }

    DPRINT1("CYW: firmware is up: protocol %u, flags 0x%08lx, %u-byte DMA indices, rx offset %lu, %u rx buffers\n",
            Pcie->SharedVersion, Pcie->SharedFlags, Pcie->IndexSize, Pcie->RxDataOffset,
            Pcie->MaxRxBufPost);

    if (Pcie->SharedVersion < 6)
    {
        return STATUS_SUCCESS;
    }

    Pcie->MailboxViaControl = (Pcie->SharedFlags & CYW_SHARED_USE_MAILBOX) == 0;
    HostCap = Pcie->SharedVersion | CYW_HOSTCAP_DS_NO_OOB_DW;
    if (Pcie->SharedFlags & CYW_SHARED_HOSTRDY_DB1)
    {
        HostCap |= CYW_HOSTCAP_H2D_ENABLE_HOSTRDY;
    }
    if (Pcie->SharedFlags & CYW_SHARED_DAR)
    {
        HostCap |= CYW_HOSTCAP_H2D_DAR;
    }
    CywPcieWriteTcm32(Pcie, Shared + CYW_SHARED_HOST_CAP, HostCap);
    CywPcieWriteTcm32(Pcie, Shared + CYW_SHARED_HOST_CAP2, 0);
    return STATUS_SUCCESS;
}

static
NTSTATUS
CywPcieDownload(
    _In_ PCYW_ADAPTER Adapter,
    _In_reads_bytes_(FirmwareSize) const UCHAR *Firmware,
    _In_ ULONG FirmwareSize,
    _In_reads_bytes_(NvramSize) const UCHAR *Nvram,
    _In_ ULONG NvramSize,
    _In_reads_bytes_opt_(SignatureSize) const UCHAR *Signature,
    _In_ ULONG SignatureSize)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    ULONG RamEnd = Adapter->RamBase + Adapter->RamSize;
    ULONG Marker = RamEnd - sizeof(ULONG);
    ULONG ResetVector;
    ULONG Written;
    ULONG Shared;
    ULONG Address;
    ULONG Waited;
    LARGE_INTEGER Delay;
    NTSTATUS Status;

    if (!CywPcieTcmRange(Pcie, Adapter->RamBase, Adapter->RamSize))
    {
        DPRINT1("CYW: BAR2 (0x%lx bytes) does not cover the dongle RAM at 0x%lx+0x%lx\n",
                Pcie->TcmLength, Adapter->RamBase, Adapter->RamSize);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (FirmwareSize < sizeof(ULONG) || NvramSize > Adapter->RamSize ||
        FirmwareSize > Adapter->RamSize - NvramSize)
    {
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    CywPcieCoreWrite32(Pcie, Pcie->MailboxMaskReg, 0);
    CywPcieCoreWrite32(Pcie, Pcie->MailboxIntReg, MAXULONG);
    CywPcieConfigWrite(Adapter, CYW_PCICFG_INTMASK, 0);
    CywPcieIndirectWrite(Pcie, PCIE2_CFG_TLCNTRL_5, MAXULONG);

    CywPcieCopyToTcm(Pcie, Adapter->RamBase, Firmware, FirmwareSize);
    RtlCopyMemory(&ResetVector, Firmware, sizeof(ResetVector));
    Pcie->FirmwareSize = FirmwareSize;
    CywPcieWriteTcm32(Pcie, Marker, 0);

    Address = RamEnd - NvramSize;
    CywPcieCopyToTcm(Pcie, Address, Nvram, NvramSize);
    if (Pcie->UseSeed)
    {
        Status = CywPcieAddFooters(Adapter, &Address, Signature, SignatureSize);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CYW: the firmware footers could not be placed (0x%08lx)\n", Status);
        }
    }
    {
        ULONG ClearStart = ALIGN_UP_BY(Adapter->RamBase + FirmwareSize, sizeof(ULONG));

        if (Address > ClearStart)
        {
            CywPcieCopyToTcm(Pcie, ClearStart, NULL, Address - ClearStart);
        }
    }

    Written = CywPcieReadTcm32(Pcie, Marker);
    if (!Pcie->SkipResetVector)
    {
        CywPcieWriteTcm32(Pcie, 0, ResetVector);
    }
    Status = CywChipResetCore(Adapter, Pcie->Cpu->Wrap, ARMCR4_BCMA_IOCTL_CPUHALT, 0, 0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Shared = Written;
    Delay.QuadPart = -50LL * 10000LL;
    for (Waited = 0; Waited < CYW_FW_UP_TIMEOUT_MS && Shared == Written; Waited += 50)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        Shared = CywPcieReadTcm32(Pcie, Marker);
    }
    if (Shared == Written)
    {
        DPRINT1("CYW: the firmware did not start within %u ms\n", CYW_FW_UP_TIMEOUT_MS);
        return STATUS_IO_TIMEOUT;
    }
    if (Shared < Adapter->RamBase || Shared >= RamEnd)
    {
        DPRINT1("CYW: the firmware published an invalid shared area 0x%08lx\n", Shared);
        return STATUS_DEVICE_DATA_ERROR;
    }
    DPRINT1("CYW: the firmware started after %lu ms, shared area at 0x%08lx\n", Waited, Shared);
    return CywPcieReadShared(Adapter, Shared);
}

VOID
CywPcieReadConsole(
    _In_ PCYW_PCIE Pcie)
{
    ULONG WriteIndex;
    ULONG Budget = 4096;

    if (Pcie->ConsoleSize == 0 ||
        InterlockedCompareExchange(&Pcie->ConsoleBusy, 1, 0) != 0)
    {
        return;
    }
    WriteIndex = CywPcieReadTcm32(Pcie, Pcie->ConsoleIndexAddress);
    while (WriteIndex < Pcie->ConsoleSize && Pcie->ConsoleRead != WriteIndex && Budget-- != 0)
    {
        ULONG Address = Pcie->ConsoleBuffer + Pcie->ConsoleRead;
        CHAR Ch = (CHAR)READ_REGISTER_UCHAR(Pcie->Tcm + Address);

        Pcie->ConsoleRead = (Pcie->ConsoleRead + 1) % Pcie->ConsoleSize;
        if (Ch == '\r')
        {
            continue;
        }
        if (Ch != '\n' && Pcie->ConsoleLineLength < sizeof(Pcie->ConsoleLine) - 1)
        {
            Pcie->ConsoleLine[Pcie->ConsoleLineLength++] = Ch;
            continue;
        }
        Pcie->ConsoleLine[Pcie->ConsoleLineLength] = ANSI_NULL;
        if (Pcie->ConsoleLineLength != 0)
        {
            DPRINT1("CYW: firmware: %s\n", Pcie->ConsoleLine);
        }
        Pcie->ConsoleLineLength = 0;
        if (Ch != '\n')
        {
            Pcie->ConsoleLine[Pcie->ConsoleLineLength++] = Ch;
        }
    }
    InterlockedExchange(&Pcie->ConsoleBusy, 0);
}

VOID
CywPcieRingDoorbell(
    _In_ PCYW_PCIE Pcie)
{
    KeMemoryBarrier();
    CywPcieCoreWrite32(Pcie, (Pcie->SharedFlags & CYW_SHARED_DAR) ?
                       PCIE2_REG64_H2D_MAILBOX_0 : PCIE2_REG_H2D_MAILBOX_0, 1);
}

VOID
CywPcieSetInterrupts(
    _In_ PCYW_PCIE Pcie,
    _In_ BOOLEAN Enable)
{
    InterlockedExchange(&Pcie->InterruptsEnabled, Enable ? 1 : 0);
    CywPcieCoreWrite32(Pcie, Pcie->MailboxMaskReg,
                       (Enable && Pcie->InterruptHandle != NULL) ?
                       (Pcie->D2hDoorbellMask | Pcie->Fn0Mask) : 0);
}

static
NTSTATUS
CywPcieSendMailbox(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Data)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    LARGE_INTEGER Delay;
    ULONG Retry;

    if (Pcie->MailboxViaControl)
    {
        return CywMsgbufSendMailbox(Adapter, Data);
    }

    Delay.QuadPart = -10LL * 10000LL;
    for (Retry = 0; CywPcieReadTcm32(Pcie, Pcie->HtodMailboxAddress) != 0; Retry++)
    {
        if (Retry == 100)
        {
            return STATUS_IO_TIMEOUT;
        }
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    CywPcieWriteTcm32(Pcie, Pcie->HtodMailboxAddress, Data);
    CywPcieConfigWrite(Adapter, CYW_PCICFG_SBMBX, 1);
    if (Pcie->Pcie2->Rev <= 13)
    {
        CywPcieConfigWrite(Adapter, CYW_PCICFG_SBMBX, 1);
    }
    return STATUS_SUCCESS;
}

VOID
CywPcieHandleMailbox(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Data)
{
    PCYW_PCIE Pcie = Adapter->Pcie;

    if (Data & CYW_D2H_DEV_DS_ENTER_REQ)
    {
        NTSTATUS Status = CywPcieSendMailbox(Adapter, CYW_H2D_HOST_DS_ACK);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("CYW: acknowledging the deep sleep request failed (0x%08lx)\n", Status);
        }
    }
    if ((Data & CYW_D2H_DEV_FWHALT) && !Pcie->FirmwareHalted)
    {
        Pcie->FirmwareHalted = TRUE;
        Adapter->FirmwareHalted = TRUE;
        DPRINT1("CYW: the firmware halted\n");
        CywPcieReadConsole(Pcie);
        InterlockedExchange(&Pcie->FirmwareFailed, 1);
    }
}

VOID
CywPcieServiceMailbox(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    ULONG Status;
    ULONG Data;

    Status = CywPcieCoreRead32(Pcie, Pcie->MailboxIntReg);
    if (Status == 0 || Status == MAXULONG)
    {
        return;
    }
    CywPcieCoreWrite32(Pcie, Pcie->MailboxIntReg, Status);
    if ((Status & Pcie->Fn0Mask) != 0 && Pcie->DtohMailboxAddress != 0)
    {
        Data = CywPcieReadTcm32(Pcie, Pcie->DtohMailboxAddress);
        if (Data != 0 && Data != MAXULONG)
        {
            CywPcieWriteTcm32(Pcie, Pcie->DtohMailboxAddress, 0);
            CywPcieHandleMailbox(Adapter, Data);
        }
    }
}

static
BOOLEAN
NTAPI
CywPcieIsr(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportInterruptContext;
    PCYW_PCIE Pcie = Adapter->Pcie;
    ULONG Status;

    *QueueDefaultInterruptDpc = FALSE;
    *TargetProcessors = 0;
    if (Pcie == NULL || Pcie->Regs == NULL)
    {
        return FALSE;
    }

    Status = CywPcieCoreRead32(Pcie, Pcie->MailboxIntReg);
    if ((Status == 0 || Status == MAXULONG) && !Pcie->MessageInterrupt)
    {
        return FALSE;
    }
    CywPcieCoreWrite32(Pcie, Pcie->MailboxMaskReg, 0);
    InterlockedExchange(&Pcie->InterruptPending, 1);
    *QueueDefaultInterruptDpc = TRUE;
    return TRUE;
}

static
VOID
NTAPI
CywPcieInterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportInterruptContext;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
    UNREFERENCED_PARAMETER(NdisReserved2);

    KeSetEvent(&Adapter->BusEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
VOID
NTAPI
CywPcieDisableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportInterruptContext;

    CywPcieCoreWrite32(Adapter->Pcie, Adapter->Pcie->MailboxMaskReg, 0);
}

static
VOID
NTAPI
CywPcieEnableInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportInterruptContext;
    PCYW_PCIE Pcie = Adapter->Pcie;

    if (InterlockedCompareExchange(&Pcie->InterruptsEnabled, 0, 0) != 0)
    {
        CywPcieCoreWrite32(Pcie, Pcie->MailboxMaskReg, Pcie->D2hDoorbellMask | Pcie->Fn0Mask);
    }
}

static
BOOLEAN
NTAPI
CywPcieMessageIsr(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportInterruptContext;
    PCYW_PCIE Pcie = Adapter->Pcie;

    UNREFERENCED_PARAMETER(MessageId);

    *QueueDefaultInterruptDpc = FALSE;
    *TargetProcessors = 0;
    if (Pcie == NULL || Pcie->Regs == NULL)
    {
        return FALSE;
    }
    CywPcieCoreWrite32(Pcie, Pcie->MailboxMaskReg, 0);
    InterlockedExchange(&Pcie->InterruptPending, 1);
    *QueueDefaultInterruptDpc = TRUE;
    return TRUE;
}

static
VOID
NTAPI
CywPcieMessageDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2)
{
    UNREFERENCED_PARAMETER(MessageId);

    CywPcieInterruptDpc(MiniportInterruptContext, MiniportDpcContext,
                        ReceiveThrottleParameters, NdisReserved2);
}

static
VOID
NTAPI
CywPcieDisableMessageInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId)
{
    UNREFERENCED_PARAMETER(MessageId);

    CywPcieDisableInterrupt(MiniportInterruptContext);
}

static
VOID
NTAPI
CywPcieEnableMessageInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ ULONG MessageId)
{
    UNREFERENCED_PARAMETER(MessageId);

    CywPcieEnableInterrupt(MiniportInterruptContext);
}

static
NTSTATUS
CywPcieRegisterInterrupt(
    _In_ PCYW_ADAPTER Adapter)
{
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS Characteristics;
    NDIS_STATUS Status;

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    Characteristics.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    Characteristics.InterruptHandler = CywPcieIsr;
    Characteristics.InterruptDpcHandler = CywPcieInterruptDpc;
    Characteristics.DisableInterruptHandler = CywPcieDisableInterrupt;
    Characteristics.EnableInterruptHandler = CywPcieEnableInterrupt;
    Characteristics.MsiSupported = TRUE;
    Characteristics.MessageInterruptHandler = CywPcieMessageIsr;
    Characteristics.MessageInterruptDpcHandler = CywPcieMessageDpc;
    Characteristics.DisableMessageInterruptHandler = CywPcieDisableMessageInterrupt;
    Characteristics.EnableMessageInterruptHandler = CywPcieEnableMessageInterrupt;

    Status = NdisMRegisterInterruptEx(Adapter->MiniportAdapterHandle, Adapter,
                                      &Characteristics, &Adapter->Pcie->InterruptHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("CYW: registering the interrupt failed (0x%08lx)\n", Status);
        Adapter->Pcie->InterruptHandle = NULL;
        return STATUS_UNSUCCESSFUL;
    }
    Adapter->Pcie->MessageInterrupt = (Characteristics.InterruptType == NDIS_CONNECT_MESSAGE_BASED);
    DPRINT1("CYW: %s interrupt connected\n", Adapter->Pcie->MessageInterrupt ? "message" : "line");
    return STATUS_SUCCESS;
}

NTSTATUS
CywPcieAllocateDma(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Length,
    _Out_ PCYW_DMA_BLOCK Block)
{
    PVOID Va = NULL;
    NDIS_PHYSICAL_ADDRESS Pa;

    RtlZeroMemory(Block, sizeof(*Block));
    Pa.QuadPart = 0;
    NdisMAllocateSharedMemory(Adapter->MiniportAdapterHandle, Length, FALSE, &Va, &Pa);
    if (Va == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Va, Length);
    Block->Va = Va;
    Block->Pa = Pa;
    Block->Length = Length;
    return STATUS_SUCCESS;
}

VOID
CywPcieFreeDma(
    _In_ PCYW_ADAPTER Adapter,
    _Inout_ PCYW_DMA_BLOCK Block)
{
    if (Block->Va != NULL)
    {
        NdisMFreeSharedMemory(Adapter->MiniportAdapterHandle, Block->Length, FALSE, Block->Va, Block->Pa);
    }
    RtlZeroMemory(Block, sizeof(*Block));
}

NTSTATUS
CywPcieOpen(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNDIS_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Regs = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Tcm = NULL;
    PCYW_PCIE Pcie;
    ULONG Identity;
    ULONG Command;
    ULONG Index;
    NDIS_STATUS Status;

    for (Index = 0; Index < Resources->Count; Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = &Resources->PartialDescriptors[Index];

        if (Descriptor->Type != CmResourceTypeMemory)
        {
            continue;
        }
        if (Regs == NULL || Descriptor->u.Memory.Length < Regs->u.Memory.Length)
        {
            Regs = Descriptor;
        }
        if (Tcm == NULL || Descriptor->u.Memory.Length > Tcm->u.Memory.Length)
        {
            Tcm = Descriptor;
        }
    }
    if (Regs == NULL || Tcm == NULL || Regs == Tcm ||
        Regs->u.Memory.Length < CYW_BAR0_PCIE2_REGS + 0x1000)
    {
        DPRINT1("CYW: the PCIe dongle has no register and memory windows\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Pcie = CywAllocate(sizeof(CYW_PCIE));
    if (Pcie == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Adapter->Pcie = Pcie;
    Adapter->BusType = CywBusPcie;
    Pcie->Window = MAXULONG;
    KeInitializeSpinLock(&Pcie->SubmitLock);
    KeInitializeSpinLock(&Pcie->TxLock);
    KeInitializeEvent(&Pcie->IoctlEvent, NotificationEvent, FALSE);

    Pcie->RegsLength = min(Regs->u.Memory.Length, CYW_BAR0_MAP_LENGTH);
    Status = NdisMMapIoSpace((PVOID *)&Pcie->Regs, Adapter->MiniportAdapterHandle,
                             Regs->u.Memory.Start, Pcie->RegsLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Pcie->Regs = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Pcie->TcmLength = Tcm->u.Memory.Length;
    Status = NdisMMapIoSpace((PVOID *)&Pcie->Tcm, Adapter->MiniportAdapterHandle,
                             Tcm->u.Memory.Start, Pcie->TcmLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Pcie->Tcm = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!NT_SUCCESS(CywPcieConfigRead(Adapter, 0, &Identity)) ||
        (Identity & 0xFFFF) != CYW_PCI_VENDOR_BROADCOM)
    {
        DPRINT1("CYW: PCI configuration space reads 0x%08lx, not a Broadcom device\n", Identity);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    Pcie->DeviceId = (USHORT)(Identity >> 16);
    if (NT_SUCCESS(CywPcieConfigRead(Adapter, 4, &Command)) && (Command & 0x6) != 0x6)
    {
        CywPcieConfigWrite(Adapter, 4, (Command & 0xFFFF) | 0x6);
    }

    DPRINT1("CYW: PCIe dongle 14e4:%04x, registers 0x%I64x (0x%lx), memory 0x%I64x (0x%lx)\n",
            Pcie->DeviceId, Regs->u.Memory.Start.QuadPart, Regs->u.Memory.Length,
            Tcm->u.Memory.Start.QuadPart, Tcm->u.Memory.Length);
    return STATUS_SUCCESS;
}

VOID
CywPcieClose(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;

    if (Pcie == NULL)
    {
        return;
    }
    if (Pcie->Regs != NULL)
    {
        CywPcieSetInterrupts(Pcie, FALSE);
    }
    if (Pcie->InterruptHandle != NULL)
    {
        NdisMDeregisterInterruptEx(Pcie->InterruptHandle);
        Pcie->InterruptHandle = NULL;
    }
    InterlockedExchange(&Pcie->Up, 0);
    if (Pcie->Regs != NULL && Pcie->Chipcommon != NULL && Pcie->Pcie2 != NULL && Pcie->Cpu != NULL)
    {
        CywPcieSetPassive(Adapter);
        CywPcieWatchdogReset(Adapter);
    }
    CywMsgbufRelease(Adapter);
    if (Pcie->Tcm != NULL)
    {
        NdisMUnmapIoSpace(Adapter->MiniportAdapterHandle, Pcie->Tcm, Pcie->TcmLength);
    }
    if (Pcie->Regs != NULL)
    {
        NdisMUnmapIoSpace(Adapter->MiniportAdapterHandle, Pcie->Regs, Pcie->RegsLength);
    }
    Adapter->ClmPath = NULL;
    Adapter->TxcapPath = NULL;
    CywFree(Pcie->ClmPath);
    CywFree(Pcie->TxcapPath);
    CywFree(Pcie);
    Adapter->Pcie = NULL;
}

VOID
CywPcieShutdown(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;

    if (Pcie == NULL || Pcie->Regs == NULL)
    {
        return;
    }
    InterlockedExchange(&Pcie->Up, 0);
    CywPcieSetInterrupts(Pcie, FALSE);
    if (Pcie->Chipcommon != NULL && Pcie->Pcie2 != NULL && Pcie->Cpu != NULL)
    {
        CywPcieSetPassive(Adapter);
    }
}

static
NTSTATUS
CywPcieBuildNvram(
    _In_ PCYW_PCIE Pcie,
    _In_reads_bytes_(RawSize) const UCHAR *Raw,
    _In_ ULONG RawSize,
    _Outptr_ PUCHAR *Nvram,
    _Out_ PULONG NvramSize)
{
    CHAR MacEntry[32];
    PUCHAR Stripped;
    PUCHAR Image;
    ULONG Length;
    ULONG Token;
    NTSTATUS Status;

    MacEntry[0] = ANSI_NULL;
    if (Pcie->HaveMacAddress)
    {
        RtlStringCbPrintfA(MacEntry, sizeof(MacEntry), "macaddr=%02x:%02x:%02x:%02x:%02x:%02x",
                           Pcie->MacAddress[0], Pcie->MacAddress[1], Pcie->MacAddress[2],
                           Pcie->MacAddress[3], Pcie->MacAddress[4], Pcie->MacAddress[5]);
    }
    Status = CywStripNvram(Raw, RawSize, MacEntry[0] ? MacEntry : NULL, &Stripped, &Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Length / sizeof(ULONG) > MAXUSHORT)
    {
        CywFree(Stripped);
        return STATUS_INTEGER_OVERFLOW;
    }
    Image = CywAllocate(Length + sizeof(Token));
    if (Image == NULL)
    {
        CywFree(Stripped);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Token = CYW_NVRAM_TOKEN(Length / sizeof(ULONG));
    RtlCopyMemory(Image, Stripped, Length);
    RtlCopyMemory(Image + Length, &Token, sizeof(Token));
    CywFree(Stripped);
    *Nvram = Image;
    *NvramSize = Length + sizeof(Token);
    return STATUS_SUCCESS;
}

NTSTATUS
CywPcieBringUp(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    const CYW_PCIE_CHIP_INFO *Chip = NULL;
    PCSTR BaseName = NULL;
    CYW_PCIE_BOARDS Boards;
    PUCHAR Firmware = NULL;
    PUCHAR Signature = NULL;
    PUCHAR RawNvram = NULL;
    PUCHAR Nvram = NULL;
    ULONG FirmwareSize = 0;
    ULONG SignatureSize = 0;
    ULONG RawNvramSize = 0;
    ULONG NvramSize = 0;
    ULONG ChipIdentity;
    ULONG Index;
    ULONG Stale;
    NTSTATUS Status;

    Status = CywPcieBackplaneReadl(Adapter, SI_ENUM_BASE_DEFAULT, &ChipIdentity);
    if (!NT_SUCCESS(Status) || ChipIdentity == MAXULONG)
    {
        DPRINT1("CYW: the chip identity read failed (0x%08lx, 0x%08lx)\n", Status, ChipIdentity);
        return NT_SUCCESS(Status) ? STATUS_DEVICE_NOT_CONNECTED : Status;
    }
    Adapter->ChipId = ChipIdentity & CID_ID_MASK;
    Adapter->ChipRev = (ChipIdentity & CID_REV_MASK) >> CID_REV_SHIFT;
    for (Index = 0; Index < RTL_NUMBER_OF(CywPcieChips); Index++)
    {
        if (CywPcieChips[Index].ChipId == Adapter->ChipId)
        {
            Chip = &CywPcieChips[Index];
        }
    }
    for (Index = 0; Index < RTL_NUMBER_OF(CywPcieFirmwareNames); Index++)
    {
        if (CywPcieFirmwareNames[Index].ChipId == Adapter->ChipId &&
            Adapter->ChipRev < 32 &&
            (CywPcieFirmwareNames[Index].RevisionMask & (1UL << Adapter->ChipRev)) != 0)
        {
            BaseName = CywPcieFirmwareNames[Index].BaseName;
        }
    }
    DPRINT1("CYW: BCM%lx revision %lu on PCIe\n", Adapter->ChipId, Adapter->ChipRev);
    if (Chip == NULL || BaseName == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }
    Adapter->RamBase = Chip->RamBase;
    Pcie->UseSeed = TRUE;

    Status = CywPcieScanCores(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Pcie->Pcie2->Rev >= PCIE2_REG64_FIRST_REV)
    {
        Pcie->MailboxIntReg = PCIE2_REG64_MAILBOXINT;
        Pcie->MailboxMaskReg = PCIE2_REG64_MAILBOXMASK;
        Pcie->D2hDoorbellMask = PCIE2_MB64_INT_D2H_DB;
        Pcie->Fn0Mask = 0;
    }
    else
    {
        Pcie->MailboxIntReg = PCIE2_REG_MAILBOXINT;
        Pcie->MailboxMaskReg = PCIE2_REG_MAILBOXMASK;
        Pcie->D2hDoorbellMask = PCIE2_MB_INT_D2H_DB;
        Pcie->Fn0Mask = PCIE2_MB_INT_FN0;
    }
    DPRINT1("CYW: %lu cores, chipcommon rev %u, PCIe rev %u, %s rev %u\n",
            Pcie->CoreCount, Pcie->Chipcommon->Rev, Pcie->Pcie2->Rev,
            Adapter->CpuCoreType == CywCpuCoreArmCr4 ? "Cortex-R4" : "Cortex-A7", Pcie->Cpu->Rev);

    Status = CywPcieSetPassive(Adapter);
    if (NT_SUCCESS(Status))
    {
        Status = CywPcieWatchdogReset(Adapter);
    }
    if (NT_SUCCESS(Status))
    {
        Status = CywPcieSetPassive(Adapter);
    }
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CYW: resetting the dongle failed (0x%08lx)\n", Status);
        return Status;
    }
    Stale = CywPcieCoreRead32(Pcie, Pcie->MailboxIntReg);
    if (Stale != MAXULONG)
    {
        CywPcieCoreWrite32(Pcie, Pcie->MailboxIntReg, Stale);
    }

    Status = CywPcieMeasureRam(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    CywPcieReadOtp(Adapter, Chip);
    CywPcieReadPlatform(Adapter);
    CywPcieIndirectWrite(Pcie, PCIE2_CFG_BAR2_CONFIG, CywPcieIndirectRead(Pcie, PCIE2_CFG_BAR2_CONFIG));

    CywPcieBuildBoards(Pcie, &Boards);
    Status = CywPcieLoadFile(BaseName, &Boards, ".bin", &Firmware, &FirmwareSize, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CYW: no %s firmware for board '%s' (0x%08lx)\n",
                BaseName, Pcie->BoardType, Status);
        goto Done;
    }
    Status = CywPcieLoadFile(BaseName, &Boards, ".txt", &RawNvram, &RawNvramSize, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CYW: no NVRAM matches board '%s' module '%s' vendor '%s' (0x%08lx)\n",
                Pcie->BoardType, Pcie->OtpModule, Pcie->OtpVendor, Status);
        goto Done;
    }
    if (!NT_SUCCESS(CywPcieLoadFile(BaseName, &Boards, ".sig", &Signature, &SignatureSize, NULL)))
    {
        Signature = NULL;
        SignatureSize = 0;
    }
    if (NT_SUCCESS(CywPcieLoadFile(BaseName, &Boards, ".clm_blob", NULL, NULL, &Pcie->ClmPath)))
    {
        Adapter->ClmPath = Pcie->ClmPath;
    }
    if (NT_SUCCESS(CywPcieLoadFile(BaseName, &Boards, ".txcap_blob", NULL, NULL, &Pcie->TxcapPath)))
    {
        Adapter->TxcapPath = Pcie->TxcapPath;
    }

    if (FirmwareSize >= CYW_RAMSIZE_MAGIC_OFFSET + 2 * sizeof(ULONG))
    {
        ULONG Magic;
        ULONG Size;

        RtlCopyMemory(&Magic, Firmware + CYW_RAMSIZE_MAGIC_OFFSET, sizeof(Magic));
        RtlCopyMemory(&Size, Firmware + CYW_RAMSIZE_MAGIC_OFFSET + sizeof(Magic), sizeof(Size));
        if (Magic == CYW_RAMSIZE_MAGIC && Size != 0 && Size <= CYW_CHIP_MAX_MEMSIZE)
        {
            Adapter->RamSize = Size;
        }
    }
    DPRINT1("CYW: dongle RAM 0x%lx bytes at 0x%lx\n", Adapter->RamSize, Adapter->RamBase);

    Status = CywPcieBuildNvram(Pcie, RawNvram, RawNvramSize, &Nvram, &NvramSize);
    if (!NT_SUCCESS(Status))
    {
        goto Done;
    }

    if (!NT_SUCCESS(CywPcieRegisterInterrupt(Adapter)))
    {
        DPRINT1("CYW: no interrupt connected, the dongle runs in polled mode\n");
    }

    Status = CywPcieDownload(Adapter, Firmware, FirmwareSize, Nvram, NvramSize,
                             Signature, SignatureSize);
    CywFree(Firmware);
    Firmware = NULL;
    if (!NT_SUCCESS(Status))
    {
        CywPcieReadConsole(Pcie);
        goto Done;
    }

    Status = CywMsgbufInitialize(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CYW: setting up the message rings failed (0x%08lx)\n", Status);
        goto Done;
    }
    InterlockedExchange(&Pcie->Up, 1);

    Status = CywStartBusThread(Adapter);
    if (!NT_SUCCESS(Status))
    {
        goto Done;
    }
    CywPcieSetInterrupts(Pcie, TRUE);
    if (Pcie->SharedFlags & CYW_SHARED_HOSTRDY_DB1)
    {
        CywPcieCoreWrite32(Pcie, (Pcie->SharedFlags & CYW_SHARED_DAR) ?
                           PCIE2_REG64_H2D_MAILBOX_1 : PCIE2_REG_H2D_MAILBOX_1, 1);
    }

    Status = CywChipConfigureFirmware(Adapter);
    CywPcieReadConsole(Pcie);
    if (NT_SUCCESS(Status))
    {
        DPRINT1("CYW: PCIe dongle ready, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                Adapter->CurrentAddress[0], Adapter->CurrentAddress[1], Adapter->CurrentAddress[2],
                Adapter->CurrentAddress[3], Adapter->CurrentAddress[4], Adapter->CurrentAddress[5]);
    }
    else
    {
        DPRINT1("CYW: configuring the firmware failed (0x%08lx)\n", Status);
    }

Done:
    CywFree(Firmware);
    CywFree(Signature);
    CywFree(RawNvram);
    CywFree(Nvram);
    return Status;
}

BOOLEAN
CywPcieFirmwareFailed(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;

    if (Pcie == NULL)
    {
        return FALSE;
    }
    return InterlockedCompareExchange(&Pcie->FirmwareFailed, 0, 0) != 0 &&
           InterlockedCompareExchange(&Pcie->Recovering, 0, 0) == 0;
}

NTSTATUS
CywPcieRecover(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_PCIE Pcie = Adapter->Pcie;
    NTSTATUS Status;

    if (Pcie == NULL || Pcie->Regs == NULL)
    {
        return STATUS_DEVICE_NOT_READY;
    }
    if (InterlockedCompareExchange(&Pcie->Recovering, 1, 0) != 0)
    {
        return STATUS_DEVICE_BUSY;
    }

    DPRINT1("CYW: reloading the firmware after a failure\n");
    CywIndicateDisassociation(Adapter);
    CywIndicateLinkState(Adapter, FALSE, 0);

    InterlockedExchange(&Pcie->Up, 0);
    CywPcieSetInterrupts(Pcie, FALSE);
    CywAbortPendingOids(Adapter, NDIS_STATUS_RESET_IN_PROGRESS);
    CywStopBusThread(Adapter);
    CywDrainTxQueue(Adapter);
    CywMsgbufRelease(Adapter);

    Pcie->FirmwareHalted = FALSE;
    Adapter->FirmwareHalted = FALSE;
    Pcie->SharedAddress = 0;
    InterlockedExchange(&Pcie->FlowRingState, 0);
    InterlockedExchange(&Pcie->FlowRingRefused, 0);

    Status = CywPcieBringUp(Adapter);
    if (NT_SUCCESS(Status))
    {
        InterlockedExchange(&Pcie->FirmwareFailed, 0);
        DPRINT1("CYW: the firmware is running again\n");
    }
    else
    {
        DPRINT1("CYW: reloading the firmware failed (0x%08lx)\n", Status);
    }
    InterlockedExchange(&Pcie->Recovering, 0);
    return Status;
}
