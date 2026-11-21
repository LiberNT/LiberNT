/*
 * PROJECT:     LiberNT SD/SDIO/eMMC Bus Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Synopsys DesignWare mobile storage host controller backend
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "dwmmc.h"

#define NDEBUG
#include <debug.h>

static ULONG
DwMmcRead(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG Register)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)FdoExtension->RegisterBase + Register));
}

static VOID
DwMmcWrite(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG Register, _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)FdoExtension->RegisterBase + Register), Value);
}

static VOID
DwMmcDelayMs(_In_ ULONG Milliseconds)
{
    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        KeStallExecutionProcessor(Milliseconds * 1000);
    }
    else
    {
        LARGE_INTEGER Delay;

        Delay.QuadPart = -(LONGLONG)Milliseconds * 10000;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
}

static BOOLEAN
DwMmcPollClear(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG Register, _In_ ULONG Mask, _In_ ULONG TimeoutMs)
{
    ULONG Budget = TimeoutMs * 100;

    for (;;)
    {
        if (!(DwMmcRead(FdoExtension, Register) & Mask))
            return TRUE;
        if (Budget == 0)
            return FALSE;
        KeStallExecutionProcessor(10);
        Budget--;
    }
}

static ULONG
DwMmcIdstsRegister(_In_ PFDO_EXTENSION FdoExtension)
{
    return FdoExtension->DwMmcIdmac64 ? DWMMC_IDSTS64 : DWMMC_IDSTS32;
}

static ULONG
DwMmcIdintenRegister(_In_ PFDO_EXTENSION FdoExtension)
{
    return FdoExtension->DwMmcIdmac64 ? DWMMC_IDINTEN64 : DWMMC_IDINTEN32;
}

static ULONG
DwMmcDescriptorControl(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG Index)
{
    ASSERT(Index < DWMMC_IDMAC_DESCRIPTORS);

    if (FdoExtension->DwMmcIdmac64)
        return ReadULongAcquire(&((PDWMMC_IDMAC_DESC64)FdoExtension->DwMmcDescriptors + Index)->Des0);
    return ReadULongAcquire(&((PDWMMC_IDMAC_DESC32)FdoExtension->DwMmcDescriptors + Index)->Des0);
}

static VOID
DwMmcStopDma(_In_ PFDO_EXTENSION FdoExtension)
{
    ASSERT(FdoExtension->DwMmcIdmac);

    DwMmcWrite(FdoExtension, DWMMC_CTRL,
               (DwMmcRead(FdoExtension, DWMMC_CTRL) & ~(DWMMC_CTRL_USE_IDMAC | DWMMC_CTRL_DMA_ENABLE)) |
                   DWMMC_CTRL_DMA_RESET);
    (VOID)DwMmcPollClear(FdoExtension, DWMMC_CTRL, DWMMC_CTRL_DMA_RESET, SD_CMD_TIMEOUT_MS);
    DwMmcWrite(FdoExtension, DWMMC_BMOD,
               (DwMmcRead(FdoExtension, DWMMC_BMOD) & ~(DWMMC_BMOD_DE | DWMMC_BMOD_FB)) | DWMMC_BMOD_SWR);
    (VOID)DwMmcPollClear(FdoExtension, DWMMC_BMOD, DWMMC_BMOD_SWR, SD_CMD_TIMEOUT_MS);
    DwMmcWrite(FdoExtension, DwMmcIdstsRegister(FdoExtension), DWMMC_IDSTS_ALL);
}

static NTSTATUS
DwMmcErrorToStatus(_In_ ULONG Interrupts)
{
    if (Interrupts & DWMMC_INT_RTO)
        return STATUS_SD_CMD_TIMEOUT;
    if (Interrupts & DWMMC_INT_RCRC)
        return STATUS_SD_CMD_CRC_ERROR;
    if (Interrupts & (DWMMC_INT_DRTO | DWMMC_INT_HTO))
        return STATUS_SD_DATA_TIMEOUT;
    if (Interrupts & (DWMMC_INT_DCRC | DWMMC_INT_EBE | DWMMC_INT_SBE))
        return STATUS_SD_DATA_CRC_ERROR;
    return STATUS_SD_IO_ERROR;
}

static NTSTATUS
DwMmcUpdateClock(_In_ PFDO_EXTENSION FdoExtension)
{
    DwMmcWrite(FdoExtension, DWMMC_CMDARG, 0);
    DwMmcWrite(FdoExtension, DWMMC_CMD, DWMMC_CMD_START | DWMMC_CMD_UPD_CLK | DWMMC_CMD_PRV_DAT_WAIT);
    if (!DwMmcPollClear(FdoExtension, DWMMC_CMD, DWMMC_CMD_START, SD_CMD_TIMEOUT_MS))
    {
        DPRINT1("[DWMMC] clock update not accepted (RINTSTS=0x%08lx)\n", DwMmcRead(FdoExtension, DWMMC_RINTSTS));
        return STATUS_IO_TIMEOUT;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
DwMmcSetClock(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG TargetClockKhz)
{
    ULONG BusKhz;
    ULONG Divider;
    ULONG ActualKhz;
    NTSTATUS Status;

    if (FdoExtension == NULL || FdoExtension->RegisterBase == NULL || FdoExtension->DwMmcBusHz == 0)
        return STATUS_INVALID_PARAMETER;

    DwMmcWrite(FdoExtension, DWMMC_CLKENA, 0);
    Status = DwMmcUpdateClock(FdoExtension);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TargetClockKhz == 0)
    {
        InterlockedExchange(&FdoExtension->CurrentClockKhz, 0);
        return STATUS_SUCCESS;
    }

    BusKhz = FdoExtension->DwMmcBusHz / 1000;
    if (TargetClockKhz >= BusKhz)
    {
        Divider = 0;
        ActualKhz = BusKhz;
    }
    else
    {
        Divider = (BusKhz + 2 * TargetClockKhz - 1) / (2 * TargetClockKhz);
        ActualKhz = BusKhz / (2 * Divider);
    }

    DwMmcWrite(FdoExtension, DWMMC_CLKDIV, Divider);
    DwMmcWrite(FdoExtension, DWMMC_CLKSRC, 0);
    Status = DwMmcUpdateClock(FdoExtension);
    if (!NT_SUCCESS(Status))
        return Status;
    DwMmcWrite(FdoExtension, DWMMC_CLKENA, DWMMC_CLKEN_ENABLE);
    Status = DwMmcUpdateClock(FdoExtension);
    if (!NT_SUCCESS(Status))
        return Status;

    DwMmcWrite(FdoExtension, DWMMC_TMOUT, DWMMC_TMOUT_MAX);
    InterlockedExchange(&FdoExtension->CurrentClockKhz, (LONG)ActualKhz);
    return STATUS_SUCCESS;
}

VOID
DwMmcSetBusWidth(_In_ PFDO_EXTENSION FdoExtension, _In_ UCHAR BusWidth)
{
    ULONG Type = DWMMC_CTYPE_1BIT;

    if (BusWidth == 8)
        Type = DWMMC_CTYPE_8BIT;
    else if (BusWidth == 4)
        Type = DWMMC_CTYPE_4BIT;
    DwMmcWrite(FdoExtension, DWMMC_CTYPE, Type);
    FdoExtension->CurrentBusWidth = (BusWidth == 8 || BusWidth == 4) ? BusWidth : 1;
}

static VOID
DwMmcInitializeIdmac(_In_ PFDO_EXTENSION FdoExtension)
{
    DEVICE_DESCRIPTION Description;
    PDMA_ADAPTER Adapter;
    ULONG Hcon, Length;
    BOOLEAN Address64;

    if (FdoExtension->DwMmcIdmac)
    {
        ASSERT(FdoExtension->DmaAdapter != NULL);
        ASSERT(FdoExtension->DwMmcDescriptors != NULL);
        DwMmcStopDma(FdoExtension);
        return;
    }

    Hcon = DwMmcRead(FdoExtension, DWMMC_HCON);
    if (DWMMC_HCON_DMA_INTERFACE(Hcon) != DWMMC_HCON_DMA_IDMAC)
        return;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    ASSERT(FdoExtension->DmaAdapter == NULL);
    ASSERT(FdoExtension->DwMmcDescriptors == NULL);

    Address64 = (Hcon & DWMMC_HCON_ADDR_64BIT) != 0;
    RtlZeroMemory(&Description, sizeof(Description));
    Description.Version = DEVICE_DESCRIPTION_VERSION;
    Description.Master = TRUE;
    Description.ScatterGather = TRUE;
    Description.Dma32BitAddresses = TRUE;
    Description.Dma64BitAddresses = Address64;
    Description.InterfaceType = (FdoExtension->BusInterfaceType == InterfaceTypeUndefined)
                                    ? Internal : FdoExtension->BusInterfaceType;
    Description.BusNumber = FdoExtension->BusNumber;
    Description.DmaWidth = Width32Bits;
    Description.DmaSpeed = Compatible;
    Description.MaximumLength = DWMMC_IDMAC_DESCRIPTORS * DWMMC_IDMAC_BUFFER_MAX;

    Adapter = IoGetDmaAdapter(FdoExtension->PhysicalDevice, &Description, &FdoExtension->DmaMapRegisters);
    if (Adapter == NULL)
    {
        FdoExtension->DmaMapRegisters = 0;
        return;
    }
    FdoExtension->DmaAdapter = Adapter;

    Length = DWMMC_IDMAC_DESCRIPTORS * (Address64 ? sizeof(DWMMC_IDMAC_DESC64) : sizeof(DWMMC_IDMAC_DESC32));
    FdoExtension->DwMmcDescriptors =
        Adapter->DmaOperations->AllocateCommonBuffer(Adapter, Length, &FdoExtension->DwMmcDescriptorsLogical, FALSE);
    if (FdoExtension->DwMmcDescriptors == NULL)
        return;
    ASSERT((FdoExtension->DwMmcDescriptorsLogical.QuadPart & (sizeof(ULONG64) - 1)) == 0);
    ASSERT(Address64 || (ULONG64)FdoExtension->DwMmcDescriptorsLogical.QuadPart + Length - 1 <= MAXULONG);

    RtlZeroMemory(FdoExtension->DwMmcDescriptors, Length);
    FdoExtension->DwMmcDescriptorsLength = Length;
    FdoExtension->DwMmcIdmac64 = Address64;
    FdoExtension->DwMmcIdmac = TRUE;
    DwMmcStopDma(FdoExtension);
}

NTSTATUS
DwMmcInitializeController(_In_ PFDO_EXTENSION FdoExtension)
{
    ULONG Version;

    if (FdoExtension->DwMmcBusHz == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    FdoExtension->SpecVersion = SDHCI_SPEC_300;
    FdoExtension->HostCapabilities = SDHCI_CAP_VOLTAGE_330 |
                                     ((FdoExtension->DwMmcBusHz / 1000000) << SDHCI_CAP_BASE_CLK_SHIFT);
    if (FdoExtension->DwMmcBusWidth == 8)
        FdoExtension->HostCapabilities |= SDHCI_CAP_8BIT_SUPPORT;
    FdoExtension->HostCapabilities2 = 0;
    FdoExtension->MaxClockFrequency = FdoExtension->DwMmcBusHz / 1000;
    FdoExtension->UseAdma2 = FALSE;
    FdoExtension->UseSdma = FALSE;
    FdoExtension->CurrentBusWidth = 1;
    InterlockedExchange(&FdoExtension->CurrentClockKhz, 0);
    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);

    DwMmcWrite(FdoExtension, DWMMC_PWREN, 1);
    DwMmcDelayMs(10);
    DwMmcWrite(FdoExtension, DWMMC_CTRL, DWMMC_CTRL_ALL_RESET);
    if (!DwMmcPollClear(FdoExtension, DWMMC_CTRL, DWMMC_CTRL_ALL_RESET, SD_CMD_TIMEOUT_MS))
        return STATUS_IO_TIMEOUT;

    DwMmcWrite(FdoExtension, DWMMC_RINTSTS, DWMMC_INT_ALL);
    DwMmcWrite(FdoExtension, DWMMC_INTMASK, 0);
    DwMmcWrite(FdoExtension, DWMMC_CTRL, DwMmcRead(FdoExtension, DWMMC_CTRL) &
                                         ~(DWMMC_CTRL_INT_ENABLE | DWMMC_CTRL_DMA_ENABLE | DWMMC_CTRL_USE_IDMAC));
    DwMmcWrite(FdoExtension, DWMMC_TMOUT, DWMMC_TMOUT_MAX);

    Version = DwMmcRead(FdoExtension, DWMMC_VERID) & 0xFFFF;
    FdoExtension->DwMmcDataOffset = (Version < DWMMC_VERSION_240A) ? DWMMC_DATA_OFFSET : DWMMC_DATA_240A_OFFSET;
    if (FdoExtension->DwMmcFifoDepth == 0)
    {
        FdoExtension->DwMmcFifoDepth =
            ((DwMmcRead(FdoExtension, DWMMC_FIFOTH) >> DWMMC_FIFOTH_RX_SHIFT) & DWMMC_FIFOTH_DEPTH_MASK) + 1;
    }
    DwMmcWrite(FdoExtension, DWMMC_FIFOTH,
               DWMMC_FIFOTH_MSIZE_8 | ((FdoExtension->DwMmcFifoDepth / 2 - 1) << DWMMC_FIFOTH_RX_SHIFT) |
               (FdoExtension->DwMmcFifoDepth / 2));
    DwMmcWrite(FdoExtension, DWMMC_CTYPE, DWMMC_CTYPE_1BIT);
    FdoExtension->DwMmcNeedInit = TRUE;

    DwMmcInitializeIdmac(FdoExtension);
    /* High speed only with the internal DMA: PIO at 49.5 MHz stops the card clock mid-block and takes data CRC errors */
    if (FdoExtension->DwMmcIdmac)
        FdoExtension->HostCapabilities |= SDHCI_CAP_HIGH_SPEED;

    DPRINT1("[DWMMC] version 0x%04lx, %lu Hz controller clock, FIFO %lu words\n",
            Version, FdoExtension->DwMmcBusHz, FdoExtension->DwMmcFifoDepth);
    return DwMmcSetClock(FdoExtension, SD_INIT_CLOCK_KHZ);
}

NTSTATUS
DwMmcReset(_In_ PFDO_EXTENSION FdoExtension)
{
    LONG ClockKhz;

    if (FdoExtension->DwMmcIdmac)
        DwMmcStopDma(FdoExtension);
    DwMmcWrite(FdoExtension, DWMMC_CTRL, DwMmcRead(FdoExtension, DWMMC_CTRL) | DWMMC_CTRL_FIFO_RESET |
                                         DWMMC_CTRL_DMA_RESET);
    if (!DwMmcPollClear(FdoExtension, DWMMC_CTRL, DWMMC_CTRL_FIFO_RESET | DWMMC_CTRL_DMA_RESET, SD_CMD_TIMEOUT_MS) ||
        (DwMmcRead(FdoExtension, DWMMC_CMD) & DWMMC_CMD_START))
    {
        DPRINT1("[DWMMC] controller stuck; resetting the command path\n");
        DwMmcWrite(FdoExtension, DWMMC_CTRL, DwMmcRead(FdoExtension, DWMMC_CTRL) | DWMMC_CTRL_ALL_RESET);
        if (!DwMmcPollClear(FdoExtension, DWMMC_CTRL, DWMMC_CTRL_ALL_RESET, SD_CMD_TIMEOUT_MS))
            return STATUS_IO_TIMEOUT;
        ClockKhz = InterlockedCompareExchange(&FdoExtension->CurrentClockKhz, 0, 0);
        DwMmcWrite(FdoExtension, DWMMC_RINTSTS, DWMMC_INT_ALL);
        (VOID)DwMmcSetClock(FdoExtension, ClockKhz != 0 ? (ULONG)ClockKhz : SD_INIT_CLOCK_KHZ);
    }
    DwMmcWrite(FdoExtension, DWMMC_RINTSTS, DWMMC_INT_ALL);
    InterlockedExchange(&FdoExtension->CommandInterruptStatus, 0);
    return STATUS_SUCCESS;
}

static NTSTATUS
DwMmcWaitNotBusy(_In_ PFDO_EXTENSION FdoExtension, _In_ ULONG TimeoutMs)
{
    return DwMmcPollClear(FdoExtension, DWMMC_STATUS, DWMMC_STATUS_BUSY, TimeoutMs) ? STATUS_SUCCESS
                                                                                     : STATUS_IO_TIMEOUT;
}

static NTSTATUS
DwMmcIssueCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _In_ ULONG DataFlags,
    _Out_writes_opt_(4) PULONG Response)
{
    ULONG ResponseKind = CommandFlags & SDHCI_CMD_RESP_MASK;
    ULONG Command = (CommandIndex & DWMMC_CMD_INDEX_MASK) | DWMMC_CMD_START | DWMMC_CMD_USE_HOLD_REG | DataFlags;
    ULONG Interrupts;
    ULONG Budget;

    if (CommandIndex == SDCMD_STOP_TRANSMISSION)
        Command |= DWMMC_CMD_STOP;
    else if (DataFlags & DWMMC_CMD_DAT_EXP)
        Command |= DWMMC_CMD_PRV_DAT_WAIT;
    if (ResponseKind != SDHCI_CMD_RESP_NONE)
        Command |= DWMMC_CMD_RESP_EXP;
    if (ResponseKind == SDHCI_CMD_RESP_136)
        Command |= DWMMC_CMD_RESP_LONG;
    if (CommandFlags & SDHCI_CMD_CRC_CHECK)
        Command |= DWMMC_CMD_RESP_CRC;
    if (FdoExtension->DwMmcNeedInit)
    {
        Command |= DWMMC_CMD_INIT;
        FdoExtension->DwMmcNeedInit = FALSE;
    }

    if ((Command & DWMMC_CMD_PRV_DAT_WAIT) && !NT_SUCCESS(DwMmcWaitNotBusy(FdoExtension, SD_DATA_TIMEOUT_MS)))
        DPRINT1("[DWMMC] CMD%u: card still busy, issuing anyway\n", CommandIndex);
    if (!DwMmcPollClear(FdoExtension, DWMMC_CMD, DWMMC_CMD_START, SD_CMD_TIMEOUT_MS))
    {
        (VOID)DwMmcReset(FdoExtension);
        return STATUS_IO_TIMEOUT;
    }

    DwMmcWrite(FdoExtension, DWMMC_RINTSTS, DWMMC_INT_CMD_DONE | DWMMC_INT_CMD_ERRORS);
    DwMmcWrite(FdoExtension, DWMMC_CMDARG, Argument);
    DwMmcWrite(FdoExtension, DWMMC_CMD, Command);

    Budget = SD_CMD_TIMEOUT_MS * 100;
    for (;;)
    {
        Interrupts = DwMmcRead(FdoExtension, DWMMC_RINTSTS);
        if (Interrupts & (DWMMC_INT_CMD_DONE | DWMMC_INT_HLE))
            break;
        if (Budget == 0)
        {
            DPRINT1("[DWMMC] CMD%u completion timed out (RINTSTS=0x%08lx)\n", CommandIndex, Interrupts);
            (VOID)DwMmcReset(FdoExtension);
            return STATUS_IO_TIMEOUT;
        }
        KeStallExecutionProcessor(10);
        Budget--;
    }
    DwMmcWrite(FdoExtension, DWMMC_RINTSTS, Interrupts & (DWMMC_INT_CMD_DONE | DWMMC_INT_CMD_ERRORS));

    if (!(CommandFlags & SDHCI_CMD_CRC_CHECK))
        Interrupts &= ~DWMMC_INT_RCRC;
    if (Interrupts & DWMMC_INT_CMD_ERRORS)
        return DwMmcErrorToStatus(Interrupts & DWMMC_INT_CMD_ERRORS);

    if (Response != NULL)
    {
        if (ResponseKind == SDHCI_CMD_RESP_136)
        {
            ULONG Rsp0 = DwMmcRead(FdoExtension, DWMMC_RESP0);
            ULONG Rsp1 = DwMmcRead(FdoExtension, DWMMC_RESP1);
            ULONG Rsp2 = DwMmcRead(FdoExtension, DWMMC_RESP2);
            ULONG Rsp3 = DwMmcRead(FdoExtension, DWMMC_RESP3);

            Response[0] = (Rsp1 << 24) | (Rsp0 >> 8);
            Response[1] = (Rsp2 << 24) | (Rsp1 >> 8);
            Response[2] = (Rsp3 << 24) | (Rsp2 >> 8);
            Response[3] = Rsp3 >> 8;
        }
        else if (ResponseKind != SDHCI_CMD_RESP_NONE)
        {
            Response[0] = DwMmcRead(FdoExtension, DWMMC_RESP0);
        }
    }

    if (ResponseKind == SDHCI_CMD_RESP_48_BUSY && !(DataFlags & DWMMC_CMD_DAT_EXP))
    {
        if (!NT_SUCCESS(DwMmcWaitNotBusy(FdoExtension, SD_DATA_TIMEOUT_MS)))
        {
            DPRINT1("[DWMMC] CMD%u busy wait timed out\n", CommandIndex);
            return STATUS_IO_TIMEOUT;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
DwMmcSendCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _Out_opt_ PULONG Response)
{
    return DwMmcIssueCommand(FdoExtension, CommandIndex, Argument, CommandFlags, 0, Response);
}

static NTSTATUS
DwMmcTransferPio(_In_ PFDO_EXTENSION FdoExtension, _In_ BOOLEAN IsRead, _Inout_ PUCHAR Buffer, _In_ ULONG TotalBytes)
{
    PULONG Fifo = (PULONG)((PUCHAR)FdoExtension->RegisterBase + FdoExtension->DwMmcDataOffset);
    ULONG Remaining = TotalBytes / sizeof(ULONG);
    ULONG Budget = SD_DATA_TIMEOUT_MS * 100;

    while (Remaining > 0)
    {
        ULONG Interrupts = DwMmcRead(FdoExtension, DWMMC_RINTSTS);
        ULONG Count = (DwMmcRead(FdoExtension, DWMMC_STATUS) >> DWMMC_STATUS_FCNT_SHIFT) & DWMMC_STATUS_FCNT_MASK;
        ULONG Burst = IsRead ? Count : (FdoExtension->DwMmcFifoDepth - min(Count, FdoExtension->DwMmcFifoDepth));

        if (Interrupts & DWMMC_INT_DATA_ERRORS)
            return DwMmcErrorToStatus(Interrupts & DWMMC_INT_DATA_ERRORS);
        if (Burst == 0)
        {
            if (Budget == 0)
            {
                DPRINT1("[DWMMC] PIO %s stalled with %lu words left (RINTSTS=0x%08lx)\n",
                        IsRead ? "read" : "write", Remaining, Interrupts);
                return STATUS_IO_TIMEOUT;
            }
            KeStallExecutionProcessor(10);
            Budget--;
            continue;
        }

        Burst = min(Burst, Remaining);
        Remaining -= Burst;
        while (Burst-- > 0)
        {
            ULONG Value;

            if (IsRead)
            {
                Value = READ_REGISTER_ULONG(Fifo);
                RtlCopyMemory(Buffer, &Value, sizeof(Value));
            }
            else
            {
                RtlCopyMemory(&Value, Buffer, sizeof(Value));
                WRITE_REGISTER_ULONG(Fifo, Value);
            }
            Buffer += sizeof(ULONG);
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
DwMmcWaitDataDone(_In_ PFDO_EXTENSION FdoExtension, _In_ BOOLEAN AutoStop)
{
    ULONG Wanted = DWMMC_INT_DATA_OVER | (AutoStop ? DWMMC_INT_ACD : 0);
    ULONG Budget = SD_DATA_TIMEOUT_MS * 100;
    ULONG Interrupts;

    for (;;)
    {
        Interrupts = DwMmcRead(FdoExtension, DWMMC_RINTSTS);
        if (Interrupts & DWMMC_INT_DATA_ERRORS)
            return DwMmcErrorToStatus(Interrupts & DWMMC_INT_DATA_ERRORS);
        if ((Interrupts & Wanted) == Wanted)
            return STATUS_SUCCESS;
        if (Budget == 0)
        {
            DPRINT1("[DWMMC] data completion timed out (RINTSTS=0x%08lx)\n", Interrupts);
            return STATUS_IO_TIMEOUT;
        }
        KeStallExecutionProcessor(10);
        Budget--;
    }
}

NTSTATUS
DwMmcExecuteDataCommand(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _In_ BOOLEAN IsRead,
    _In_ BOOLEAN SendStopCommand,
    _Inout_updates_bytes_(BlockSize * BlockCount) PVOID Buffer,
    _In_ ULONG BlockSize,
    _In_ ULONG BlockCount,
    _Out_opt_ PULONG Response)
{
    ULONG LocalResponse[4];
    ULONG DataFlags;
    NTSTATUS Status;

    if (Buffer == NULL || BlockSize == 0 || BlockCount == 0 || (BlockSize % sizeof(ULONG)) != 0)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(LocalResponse, sizeof(LocalResponse));
    DwMmcWrite(FdoExtension, DWMMC_CTRL,
               (DwMmcRead(FdoExtension, DWMMC_CTRL) & ~(DWMMC_CTRL_DMA_ENABLE | DWMMC_CTRL_USE_IDMAC)) |
                   DWMMC_CTRL_FIFO_RESET);
    if (!DwMmcPollClear(FdoExtension, DWMMC_CTRL, DWMMC_CTRL_FIFO_RESET, SD_CMD_TIMEOUT_MS))
        return STATUS_IO_TIMEOUT;
    DwMmcWrite(FdoExtension, DWMMC_RINTSTS, DWMMC_INT_ALL);
    DwMmcWrite(FdoExtension, DWMMC_BLKSIZ, BlockSize);
    DwMmcWrite(FdoExtension, DWMMC_BYTCNT, BlockSize * BlockCount);

    DataFlags = DWMMC_CMD_DAT_EXP | (IsRead ? 0 : DWMMC_CMD_DAT_WR) | (SendStopCommand ? DWMMC_CMD_SEND_STOP : 0);
    Status = DwMmcIssueCommand(FdoExtension, CommandIndex, Argument, CommandFlags, DataFlags, LocalResponse);
    if (Response != NULL)
        Response[0] = LocalResponse[0];
    if (!NT_SUCCESS(Status))
    {
        (VOID)DwMmcReset(FdoExtension);
        return Status;
    }

    Status = DwMmcTransferPio(FdoExtension, IsRead, (PUCHAR)Buffer, BlockSize * BlockCount);
    if (NT_SUCCESS(Status))
        Status = DwMmcWaitDataDone(FdoExtension, SendStopCommand);
    if (NT_SUCCESS(Status) && !IsRead)
        Status = DwMmcWaitNotBusy(FdoExtension, SD_DATA_TIMEOUT_MS);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[DWMMC] CMD%u data phase failed 0x%08lx (RINTSTS=0x%08lx)\n", CommandIndex, Status,
                DwMmcRead(FdoExtension, DWMMC_RINTSTS));
        (VOID)DwMmcReset(FdoExtension);
        if (SendStopCommand)
        {
            (VOID)DwMmcSendCommand(FdoExtension, SDCMD_STOP_TRANSMISSION, 0,
                                   SDHCI_CMD_RESP_48_BUSY | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK, NULL);
        }
    }
    DwMmcWrite(FdoExtension, DWMMC_RINTSTS, DWMMC_INT_ALL);
    return Status;
}

typedef struct _DWMMC_DMA_MAP
{
    KEVENT Event;
    PSCATTER_GATHER_LIST List;
} DWMMC_DMA_MAP, *PDWMMC_DMA_MAP;

static VOID
NTAPI
DwMmcDmaListReady(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PSCATTER_GATHER_LIST ScatterGather,
    _In_ PVOID Context)
{
    PDWMMC_DMA_MAP Map = (PDWMMC_DMA_MAP)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(ScatterGather != NULL);
    ASSERT(Map->List == NULL);

    Map->List = ScatterGather;
    KeSetEvent(&Map->Event, IO_NO_INCREMENT, FALSE);
}

static VOID
DwMmcSetDescriptor(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ ULONG Index,
    _In_ ULONG Control,
    _In_ ULONG Length,
    _In_ ULONG64 Buffer,
    _In_ ULONG64 Next)
{
    ASSERT(Index < DWMMC_IDMAC_DESCRIPTORS);
    ASSERT(Length != 0);
    ASSERT(Length <= DWMMC_IDMAC_BUFFER_MAX);
    ASSERT((Buffer & (sizeof(ULONG) - 1)) == 0);

    if (FdoExtension->DwMmcIdmac64)
    {
        PDWMMC_IDMAC_DESC64 Descriptor = (PDWMMC_IDMAC_DESC64)FdoExtension->DwMmcDescriptors + Index;

        Descriptor->Des1 = 0;
        Descriptor->Des2 = Length & DWMMC_DES_BS1_MASK;
        Descriptor->Des3 = 0;
        Descriptor->Des4 = (ULONG)Buffer;
        Descriptor->Des5 = (ULONG)(Buffer >> 32);
        Descriptor->Des6 = (ULONG)Next;
        Descriptor->Des7 = (ULONG)(Next >> 32);
        Descriptor->Des0 = Control;
    }
    else
    {
        PDWMMC_IDMAC_DESC32 Descriptor = (PDWMMC_IDMAC_DESC32)FdoExtension->DwMmcDescriptors + Index;

        ASSERT(Buffer + Length - 1 <= MAXULONG);
        ASSERT(Next <= MAXULONG);
        Descriptor->Des1 = Length & DWMMC_DES_BS1_MASK;
        Descriptor->Des2 = (ULONG)Buffer;
        Descriptor->Des3 = (ULONG)Next;
        Descriptor->Des0 = Control;
    }
}

static BOOLEAN
DwMmcBuildDescriptors(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PSCATTER_GATHER_LIST List,
    _In_ ULONG Bytes,
    _Out_ PULONG Count)
{
    ULONG64 Base = (ULONG64)FdoExtension->DwMmcDescriptorsLogical.QuadPart;
    ULONG Size = FdoExtension->DwMmcIdmac64 ? sizeof(DWMMC_IDMAC_DESC64) : sizeof(DWMMC_IDMAC_DESC32);
    ULONG Element, Used = 0, Remaining = Bytes;

    ASSERT(Bytes != 0);
    *Count = 0;

    for (Element = 0; Element < List->NumberOfElements && Remaining != 0; ++Element)
    {
        ULONG64 Address = (ULONG64)List->Elements[Element].Address.QuadPart;
        ULONG Length = min(List->Elements[Element].Length, Remaining);

        if (Length == 0 || ((Address | Length) & (sizeof(ULONG) - 1)) ||
            (!FdoExtension->DwMmcIdmac64 && Address + Length - 1 > MAXULONG))
            return FALSE;

        while (Length != 0)
        {
            ULONG Chunk = min(Length, DWMMC_IDMAC_BUFFER_MAX);

            if (Used == DWMMC_IDMAC_DESCRIPTORS)
                return FALSE;
            DwMmcSetDescriptor(FdoExtension, Used,
                               DWMMC_DES0_OWN | DWMMC_DES0_CH | DWMMC_DES0_DIC | (Used == 0 ? DWMMC_DES0_FS : 0),
                               Chunk, Address, Base + (ULONG64)(Used + 1) * Size);
            Address += Chunk;
            Length -= Chunk;
            Remaining -= Chunk;
            Used++;
        }
    }
    if (Remaining != 0 || Used == 0)
        return FALSE;

    if (FdoExtension->DwMmcIdmac64)
    {
        PDWMMC_IDMAC_DESC64 Last = (PDWMMC_IDMAC_DESC64)FdoExtension->DwMmcDescriptors + (Used - 1);

        ASSERT(Last->Des0 & DWMMC_DES0_OWN);
        Last->Des6 = 0;
        Last->Des7 = 0;
        Last->Des0 = (Last->Des0 & ~(DWMMC_DES0_CH | DWMMC_DES0_DIC)) | DWMMC_DES0_LD;
    }
    else
    {
        PDWMMC_IDMAC_DESC32 Last = (PDWMMC_IDMAC_DESC32)FdoExtension->DwMmcDescriptors + (Used - 1);

        ASSERT(Last->Des0 & DWMMC_DES0_OWN);
        Last->Des3 = 0;
        Last->Des0 = (Last->Des0 & ~(DWMMC_DES0_CH | DWMMC_DES0_DIC)) | DWMMC_DES0_LD;
    }
    *Count = Used;
    return TRUE;
}

static NTSTATUS
DwMmcRunDma(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _In_ BOOLEAN IsRead,
    _In_ BOOLEAN SendStopCommand,
    _In_ ULONG BlockSize,
    _In_ ULONG BlockCount,
    _In_ ULONG Descriptors,
    _Out_opt_ PULONG Response)
{
    ULONG64 Base = (ULONG64)FdoExtension->DwMmcDescriptorsLogical.QuadPart;
    ULONG Wanted = DWMMC_INT_DATA_OVER | (SendStopCommand ? DWMMC_INT_ACD : 0);
    ULONG DmaDone = IsRead ? DWMMC_IDSTS_RI : DWMMC_IDSTS_TI;
    ULONG Idsts = DwMmcIdstsRegister(FdoExtension);
    ULONG Budget = SD_DATA_TIMEOUT_MS * 100;
    ULONG LocalResponse[4];
    ULONG Interrupts = 0, Dma = 0, DataFlags;
    NTSTATUS Status;

    ASSERT(Descriptors != 0);
    ASSERT(Descriptors <= DWMMC_IDMAC_DESCRIPTORS);
    RtlZeroMemory(LocalResponse, sizeof(LocalResponse));
    DwMmcWrite(FdoExtension, DWMMC_CTRL,
               DwMmcRead(FdoExtension, DWMMC_CTRL) | DWMMC_CTRL_FIFO_RESET | DWMMC_CTRL_DMA_RESET);
    if (!DwMmcPollClear(FdoExtension, DWMMC_CTRL, DWMMC_CTRL_FIFO_RESET | DWMMC_CTRL_DMA_RESET, SD_CMD_TIMEOUT_MS))
        return STATUS_IO_TIMEOUT;
    DwMmcWrite(FdoExtension, DWMMC_BMOD, DWMMC_BMOD_SWR);
    if (!DwMmcPollClear(FdoExtension, DWMMC_BMOD, DWMMC_BMOD_SWR, SD_CMD_TIMEOUT_MS))
        return STATUS_IO_TIMEOUT;

    DwMmcWrite(FdoExtension, DWMMC_RINTSTS, DWMMC_INT_ALL);
    DwMmcWrite(FdoExtension, Idsts, DWMMC_IDSTS_ALL);
    /* The completion bits latch in IDSTS only when enabled here; CTRL keeps the interrupt line off */
    DwMmcWrite(FdoExtension, DwMmcIdintenRegister(FdoExtension), DWMMC_IDINTEN_COMPLETION);
    DwMmcWrite(FdoExtension, DWMMC_DBADDR, (ULONG)Base);
    if (FdoExtension->DwMmcIdmac64)
        DwMmcWrite(FdoExtension, DWMMC_DBADDRU, (ULONG)(Base >> 32));
    DwMmcWrite(FdoExtension, DWMMC_BLKSIZ, BlockSize);
    DwMmcWrite(FdoExtension, DWMMC_BYTCNT, BlockSize * BlockCount);
    DwMmcWrite(FdoExtension, DWMMC_CTRL,
               DwMmcRead(FdoExtension, DWMMC_CTRL) | DWMMC_CTRL_DMA_ENABLE | DWMMC_CTRL_USE_IDMAC);
    DwMmcWrite(FdoExtension, DWMMC_BMOD, DWMMC_BMOD_DE | DWMMC_BMOD_FB);
    DwMmcWrite(FdoExtension, DWMMC_PLDMND, 1);

    DataFlags = DWMMC_CMD_DAT_EXP | (IsRead ? 0 : DWMMC_CMD_DAT_WR) | (SendStopCommand ? DWMMC_CMD_SEND_STOP : 0);
    Status = DwMmcIssueCommand(FdoExtension, CommandIndex, Argument, CommandFlags, DataFlags, LocalResponse);
    if (Response != NULL)
        Response[0] = LocalResponse[0];

    /* TODO: wait on the controller interrupt instead of polling the DMA completion */
    while (NT_SUCCESS(Status))
    {
        Interrupts = DwMmcRead(FdoExtension, DWMMC_RINTSTS);
        Dma = DwMmcRead(FdoExtension, Idsts);
        if (Interrupts & DWMMC_INT_DATA_ERRORS)
        {
            Status = DwMmcErrorToStatus(Interrupts & DWMMC_INT_DATA_ERRORS);
            break;
        }
        if (Dma & DWMMC_IDSTS_ERRORS)
        {
            Status = DwMmcErrorToStatus(0);
            break;
        }
        if ((Interrupts & Wanted) == Wanted &&
            ((Dma & DmaDone) || !(DwMmcDescriptorControl(FdoExtension, Descriptors - 1) & DWMMC_DES0_OWN)))
            break;
        if (Budget == 0)
        {
            Status = STATUS_IO_TIMEOUT;
            break;
        }
        KeStallExecutionProcessor(10);
        Budget--;
    }
    if (NT_SUCCESS(Status) && !IsRead)
        Status = DwMmcWaitNotBusy(FdoExtension, SD_DATA_TIMEOUT_MS);

    DwMmcStopDma(FdoExtension);
    if (!NT_SUCCESS(Status))
    {
        (VOID)DwMmcReset(FdoExtension);
        if (SendStopCommand)
        {
            (VOID)DwMmcSendCommand(FdoExtension, SDCMD_STOP_TRANSMISSION, 0,
                                   SDHCI_CMD_RESP_48_BUSY | SDHCI_CMD_CRC_CHECK | SDHCI_CMD_INDEX_CHECK, NULL);
        }
    }
    DwMmcWrite(FdoExtension, DWMMC_RINTSTS, DWMMC_INT_ALL);
    return Status;
}

static NTSTATUS
DwMmcExecuteDmaRequest(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ UCHAR CommandIndex,
    _In_ ULONG Argument,
    _In_ USHORT CommandFlags,
    _In_ BOOLEAN IsRead,
    _In_ BOOLEAN SendStopCommand,
    _In_ PMDL Mdl,
    _In_ ULONG BlockSize,
    _In_ ULONG BlockCount,
    _Out_opt_ PULONG Response,
    _Out_ PBOOLEAN Mapped)
{
    ULONG Bytes = BlockSize * BlockCount;
    ULONG Descriptors;
    DWMMC_DMA_MAP Map;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(FdoExtension->DwMmcIdmac);
    ASSERT(FdoExtension->DmaAdapter != NULL);
    ASSERT(FdoExtension->DwMmcDescriptors != NULL);
    ASSERT(Bytes != 0);
    ASSERT(Bytes <= MmGetMdlByteCount(Mdl));

    *Mapped = FALSE;
    if (Bytes > DWMMC_IDMAC_DESCRIPTORS * DWMMC_IDMAC_BUFFER_MAX)
        return STATUS_SUCCESS;

    KeInitializeEvent(&Map.Event, NotificationEvent, FALSE);
    Map.List = NULL;
    Status = SdBusGetScatterGatherListAtDispatch(FdoExtension, Mdl, Bytes, !IsRead, DwMmcDmaListReady, &Map);
    if (!NT_SUCCESS(Status))
        return STATUS_SUCCESS;
    (VOID)KeWaitForSingleObject(&Map.Event, Executive, KernelMode, FALSE, NULL);
    ASSERT(Map.List != NULL);

    Status = STATUS_SUCCESS;
    if (DwMmcBuildDescriptors(FdoExtension, Map.List, Bytes, &Descriptors))
    {
        *Mapped = TRUE;
        Status = DwMmcRunDma(FdoExtension, CommandIndex, Argument, CommandFlags, IsRead, SendStopCommand,
                             BlockSize, BlockCount, Descriptors, Response);
    }
    SdBusPutScatterGatherListAtDispatch(FdoExtension, Map.List, !IsRead);
    return Status;
}

NTSTATUS
DwMmcExecuteRequest(
    _In_ PFDO_EXTENSION FdoExtension,
    _In_ PSDCMD_DESCRIPTOR CmdDesc,
    _In_ ULONG Argument,
    _Inout_opt_ PMDL Mdl,
    _In_ ULONG DataLength,
    _In_ ULONG BlockSize,
    _In_ USHORT RequestFlags,
    _Out_opt_ PULONG Response)
{
    USHORT CommandFlags = SdBusResponseTypeToFlags(CmdDesc->ResponseType);
    BOOLEAN IsRead = (CmdDesc->TransferDirection == SDTD_READ);
    BOOLEAN SendStop = (CmdDesc->TransferType == SDTT_MULTI_BLOCK);
    PVOID DataBuffer;

    if (DataLength == 0 || Mdl == NULL || CmdDesc->TransferType == SDTT_CMD_ONLY)
    {
        if ((RequestFlags & SDRP_FLAG_WAIT_FOR_BUSY) && (CommandFlags & SDHCI_CMD_RESP_MASK) == SDHCI_CMD_RESP_48)
            CommandFlags = (USHORT)((CommandFlags & ~SDHCI_CMD_RESP_MASK) | SDHCI_CMD_RESP_48_BUSY);
        return DwMmcSendCommand(FdoExtension, (UCHAR)CmdDesc->Cmd, Argument, CommandFlags, Response);
    }

    if (BlockSize == 0)
        return STATUS_INVALID_PARAMETER;

    if (FdoExtension->DwMmcIdmac && DataLength / BlockSize != 0)
    {
        BOOLEAN Mapped;
        NTSTATUS Status = DwMmcExecuteDmaRequest(FdoExtension, (UCHAR)CmdDesc->Cmd, Argument, CommandFlags, IsRead,
                                                 SendStop, Mdl, BlockSize, DataLength / BlockSize, Response,
                                                 &Mapped);
        if (Mapped)
            return Status;
        /* FIXME: an unaligned or unmappable buffer falls back to PIO, which can take data CRC errors at high speed */
    }

    DataBuffer = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
    if (DataBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    return DwMmcExecuteDataCommand(FdoExtension, (UCHAR)CmdDesc->Cmd, Argument, CommandFlags,
                                   CmdDesc->TransferDirection == SDTD_READ,
                                   CmdDesc->TransferType == SDTT_MULTI_BLOCK, DataBuffer, BlockSize,
                                   DataLength / BlockSize, Response);
}
