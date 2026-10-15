/*
 * PROJECT:     LiberNT Ralink RT2800 USB Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RT2870/RT2770 MAC, baseband and RF bring-up, channel and key programming
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rt2800usb.h"

#define NDEBUG
#include <debug.h>

#define RT_BUSY_COUNT       100
#define RT_BUSY_DELAY_US    100

typedef struct _RT_RF_CHANNEL
{
    UCHAR Channel;
    ULONG Rf1;
    ULONG Rf2;
    ULONG Rf3;
    ULONG Rf4;
} RT_RF_CHANNEL;

static const RT_RF_CHANNEL RtRfChannels[] =
{
    { 1,   0x18402ecc, 0x184c0786, 0x1816b455, 0x1800510b },
    { 2,   0x18402ecc, 0x184c0786, 0x18168a55, 0x1800519f },
    { 3,   0x18402ecc, 0x184c078a, 0x18168a55, 0x1800518b },
    { 4,   0x18402ecc, 0x184c078a, 0x18168a55, 0x1800519f },
    { 5,   0x18402ecc, 0x184c078e, 0x18168a55, 0x1800518b },
    { 6,   0x18402ecc, 0x184c078e, 0x18168a55, 0x1800519f },
    { 7,   0x18402ecc, 0x184c0792, 0x18168a55, 0x1800518b },
    { 8,   0x18402ecc, 0x184c0792, 0x18168a55, 0x1800519f },
    { 9,   0x18402ecc, 0x184c0796, 0x18168a55, 0x1800518b },
    { 10,  0x18402ecc, 0x184c0796, 0x18168a55, 0x1800519f },
    { 11,  0x18402ecc, 0x184c079a, 0x18168a55, 0x1800518b },
    { 12,  0x18402ecc, 0x184c079a, 0x18168a55, 0x1800519f },
    { 13,  0x18402ecc, 0x184c079e, 0x18168a55, 0x1800518b },
    { 14,  0x18402ecc, 0x184c07a2, 0x18168a55, 0x18005193 },
    { 36,  0x18402ecc, 0x184c099a, 0x18158a55, 0x180ed1a3 },
    { 38,  0x18402ecc, 0x184c099e, 0x18158a55, 0x180ed193 },
    { 40,  0x18402ec8, 0x184c0682, 0x18158a55, 0x180ed183 },
    { 44,  0x18402ec8, 0x184c0682, 0x18158a55, 0x180ed1a3 },
    { 46,  0x18402ec8, 0x184c0686, 0x18158a55, 0x180ed18b },
    { 48,  0x18402ec8, 0x184c0686, 0x18158a55, 0x180ed19b },
    { 52,  0x18402ec8, 0x184c068a, 0x18158a55, 0x180ed193 },
    { 54,  0x18402ec8, 0x184c068a, 0x18158a55, 0x180ed1a3 },
    { 56,  0x18402ec8, 0x184c068e, 0x18158a55, 0x180ed18b },
    { 60,  0x18402ec8, 0x184c0692, 0x18158a55, 0x180ed183 },
    { 62,  0x18402ec8, 0x184c0692, 0x18158a55, 0x180ed193 },
    { 64,  0x18402ec8, 0x184c0692, 0x18158a55, 0x180ed1a3 },
    { 100, 0x18402ec8, 0x184c06b2, 0x18178a55, 0x180ed783 },
    { 102, 0x18402ec8, 0x184c06b2, 0x18578a55, 0x180ed793 },
    { 104, 0x18402ec8, 0x185c06b2, 0x18578a55, 0x180ed1a3 },
    { 108, 0x18402ecc, 0x185c0a32, 0x18578a55, 0x180ed193 },
    { 110, 0x18402ecc, 0x184c0a36, 0x18178a55, 0x180ed183 },
    { 112, 0x18402ecc, 0x184c0a36, 0x18178a55, 0x180ed19b },
    { 116, 0x18402ecc, 0x184c0a3a, 0x18178a55, 0x180ed1a3 },
    { 118, 0x18402ecc, 0x184c0a3e, 0x18178a55, 0x180ed193 },
    { 120, 0x18402ec4, 0x184c0382, 0x18178a55, 0x180ed183 },
    { 124, 0x18402ec4, 0x184c0382, 0x18178a55, 0x180ed193 },
    { 126, 0x18402ec4, 0x184c0382, 0x18178a55, 0x180ed15b },
    { 128, 0x18402ec4, 0x184c0382, 0x18178a55, 0x180ed1a3 },
    { 132, 0x18402ec4, 0x184c0386, 0x18178a55, 0x180ed18b },
    { 134, 0x18402ec4, 0x184c0386, 0x18178a55, 0x180ed193 },
    { 136, 0x18402ec4, 0x184c0386, 0x18178a55, 0x180ed19b },
    { 140, 0x18402ec4, 0x184c038a, 0x18178a55, 0x180ed183 },
    { 149, 0x18402ec4, 0x184c038a, 0x18178a55, 0x180ed1a7 },
    { 151, 0x18402ec4, 0x184c038e, 0x18178a55, 0x180ed187 },
    { 153, 0x18402ec4, 0x184c038e, 0x18178a55, 0x180ed18f },
    { 157, 0x18402ec4, 0x184c038e, 0x18178a55, 0x180ed19f },
    { 159, 0x18402ec4, 0x184c038e, 0x18178a55, 0x180ed1a7 },
    { 161, 0x18402ec4, 0x184c0392, 0x18178a55, 0x180ed187 },
    { 165, 0x18402ec4, 0x184c0392, 0x18178a55, 0x180ed197 },
};

static
VOID
RtStallUs(
    _In_ ULONG Microseconds)
{
    KeStallExecutionProcessor(Microseconds);
}

static
VOID
RtLockRegisters(
    _In_ PRT_ADAPTER Adapter)
{
    KeWaitForSingleObject(&Adapter->RegisterLock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
RtUnlockRegisters(
    _In_ PRT_ADAPTER Adapter)
{
    KeReleaseMutex(&Adapter->RegisterLock, FALSE);
}

static
BOOLEAN
RtWaitClear(
    _In_ PRT_ADAPTER Adapter,
    _In_ USHORT Offset,
    _In_ ULONG Mask,
    _Out_opt_ PULONG Value)
{
    ULONG Read = 0, i;

    for (i = 0; i < RT_BUSY_COUNT; i++)
    {
        Read = RtRead(Adapter, Offset);
        if (Read != MAXULONG && !(Read & Mask))
        {
            if (Value)
                *Value = Read;
            return TRUE;
        }
        RtStallUs(RT_BUSY_DELAY_US);
    }

    if (Value)
        *Value = Read;
    return FALSE;
}

static
VOID
RtBbpWrite(
    _In_ PRT_ADAPTER Adapter,
    _In_ UCHAR Register,
    _In_ UCHAR Value)
{
    RtLockRegisters(Adapter);
    if (RtWaitClear(Adapter, RT_BBP_CSR_CFG, RT_BBP_CSR_CFG_BUSY, NULL))
    {
        RtWrite(Adapter,
                RT_BBP_CSR_CFG,
                Value |
                    ((ULONG)Register << RT_BBP_CSR_CFG_REGNUM_SHIFT) |
                    RT_BBP_CSR_CFG_BUSY |
                    RT_BBP_CSR_CFG_RW_MODE);
    }
    RtUnlockRegisters(Adapter);
}

static
UCHAR
RtBbpRead(
    _In_ PRT_ADAPTER Adapter,
    _In_ UCHAR Register)
{
    ULONG Value = 0;

    RtLockRegisters(Adapter);
    if (RtWaitClear(Adapter, RT_BBP_CSR_CFG, RT_BBP_CSR_CFG_BUSY, NULL))
    {
        RtWrite(Adapter,
                RT_BBP_CSR_CFG,
                ((ULONG)Register << RT_BBP_CSR_CFG_REGNUM_SHIFT) |
                    RT_BBP_CSR_CFG_BUSY |
                    RT_BBP_CSR_CFG_READ |
                    RT_BBP_CSR_CFG_RW_MODE);
        RtWaitClear(Adapter, RT_BBP_CSR_CFG, RT_BBP_CSR_CFG_BUSY, &Value);
    }
    RtUnlockRegisters(Adapter);

    return (UCHAR)(Value & RT_BBP_CSR_CFG_VALUE);
}

static
VOID
RtRfWrite(
    _In_ PRT_ADAPTER Adapter,
    _In_ ULONG Value)
{
    RtLockRegisters(Adapter);
    if (RtWaitClear(Adapter, RT_RF_CSR_CFG0, RT_RF_CSR_CFG0_BUSY, NULL))
        RtWrite(Adapter, RT_RF_CSR_CFG0, (Value & RT_RF_CSR_CFG0_VALUE) | RT_RF_CSR_CFG0_BUSY);
    RtUnlockRegisters(Adapter);
}

static
VOID
RtMcuRequest(
    _In_ PRT_ADAPTER Adapter,
    _In_ UCHAR Command,
    _In_ UCHAR Token,
    _In_ UCHAR Arg0,
    _In_ UCHAR Arg1)
{
    RtLockRegisters(Adapter);
    if (RtWaitClear(Adapter, RT_H2M_MAILBOX_CSR, RT_H2M_MAILBOX_OWNER, NULL))
    {
        RtWrite(Adapter,
                RT_H2M_MAILBOX_CSR,
                0x01000000 |
                    ((ULONG)Token << 16) |
                    ((ULONG)Arg1 << 8) |
                    Arg0);
        RtWrite(Adapter, RT_HOST_CMD_CSR, Command);
    }
    RtUnlockRegisters(Adapter);
}

static
ULONG
RtSetBits(
    _In_ ULONG Value,
    _In_ ULONG Mask,
    _In_ ULONG Field)
{
    ULONG Shift = 0;

    while (!(Mask & (1u << Shift)))
        Shift++;

    return (Value & ~Mask) | ((Field << Shift) & Mask);
}

static
VOID
RtModify(
    _In_ PRT_ADAPTER Adapter,
    _In_ USHORT Offset,
    _In_ ULONG Clear,
    _In_ ULONG Set)
{
    RtWrite(Adapter, Offset, (RtRead(Adapter, Offset) & ~Clear) | Set);
}

static
NTSTATUS
RtWaitCsrReady(
    _In_ PRT_ADAPTER Adapter)
{
    ULONG Value, i;

    for (i = 0; i < RT_BUSY_COUNT; i++)
    {
        Value = RtRead(Adapter, RT_MAC_CSR0);
        if (Value && Value != MAXULONG)
        {
            Adapter->ChipId = Value >> 16;
            Adapter->ChipRevision = Value & 0xffff;
            return STATUS_SUCCESS;
        }
        RtSleep(1);
    }

    return STATUS_DEVICE_NOT_READY;
}

static
NTSTATUS
RtWaitWpdmaReady(
    _In_ PRT_ADAPTER Adapter)
{
    ULONG Value, i;

    for (i = 0; i < RT_BUSY_COUNT; i++)
    {
        Value = RtRead(Adapter, RT_WPDMA_GLO_CFG);
        if (Value != MAXULONG && !(Value & (RT_WPDMA_TX_DMA_BUSY | RT_WPDMA_RX_DMA_BUSY)))
            return STATUS_SUCCESS;
        RtSleep(10);
    }

    return STATUS_DEVICE_BUSY;
}

static
VOID
RtDisableWpdma(
    _In_ PRT_ADAPTER Adapter)
{
    RtModify(Adapter,
             RT_WPDMA_GLO_CFG,
             RT_WPDMA_ENABLE_TX_DMA | RT_WPDMA_TX_DMA_BUSY | RT_WPDMA_ENABLE_RX_DMA | RT_WPDMA_RX_DMA_BUSY,
             RT_WPDMA_TX_WRITEBACK_DONE);
}

static
NTSTATUS
RtReadFirmwareFile(
    _Out_ PUCHAR* Data,
    _Out_ PULONG Length)
{
    FILE_STANDARD_INFORMATION FileInfo;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Path;
    HANDLE Handle;
    NTSTATUS Status;
    PUCHAR Buffer;

    *Data = NULL;
    *Length = 0;

    RtlInitUnicodeString(&Path, RT_FIRMWARE_PATH);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&Handle,
                          GENERIC_READ | SYNCHRONIZE,
                          &Attributes,
                          &IoStatus,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ZwQueryInformationFile(Handle, &IoStatus, &FileInfo, sizeof(FileInfo), FileStandardInformation);
    if (!NT_SUCCESS(Status) || FileInfo.EndOfFile.QuadPart != RT_FIRMWARE_SIZE)
    {
        ZwClose(Handle);
        return NT_SUCCESS(Status) ? STATUS_INVALID_IMAGE_FORMAT : Status;
    }

    Buffer = RtAllocate(RT_FIRMWARE_SIZE);
    if (!Buffer)
    {
        ZwClose(Handle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwReadFile(Handle, NULL, NULL, NULL, &IoStatus, Buffer, RT_FIRMWARE_SIZE, NULL, NULL);
    ZwClose(Handle);
    if (!NT_SUCCESS(Status) || IoStatus.Information != RT_FIRMWARE_SIZE)
    {
        RtFree(Buffer);
        return NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status;
    }

    *Data = Buffer;
    *Length = RT_FIRMWARE_SIZE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RtLoadFirmware(
    _In_ PRT_ADAPTER Adapter)
{
    ULONG Mode = 0, Length, Value, i;
    BOOLEAN AutoRun;
    NTSTATUS Status;
    PUCHAR Firmware;

    RtWrite(Adapter, RT_AUTOWAKEUP_CFG, 0);

    Status = RtWaitCsrReady(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    RtDisableWpdma(Adapter);

    Status = RtVendorRequest(Adapter, RT_USB_DEVICE_MODE, TRUE, RT_USB_MODE_AUTORUN, 0, &Mode, sizeof(Mode));
    AutoRun = NT_SUCCESS(Status) && (Mode & 3) == 2;

    if (!AutoRun)
    {
        Status = RtReadFirmwareFile(&Firmware, &Length);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("RT2800USB: firmware %ws unavailable 0x%08lx\n", RT_FIRMWARE_PATH, Status);
            return Status;
        }

        RtWriteBlock(Adapter,
                     RT_FIRMWARE_IMAGE_BASE,
                     Firmware + (Adapter->ChipId == 0x2860 || Adapter->ChipId == 0x2872 || Adapter->ChipId == 0x3070 ? 0 : RT_FIRMWARE_HALF),
                     RT_FIRMWARE_HALF);
        RtFree(Firmware);
    }

    RtWrite(Adapter, RT_H2M_MAILBOX_CID, MAXULONG);
    RtWrite(Adapter, RT_H2M_MAILBOX_STATUS, MAXULONG);

    Status = RtVendorRequest(Adapter, RT_USB_DEVICE_MODE, FALSE, RT_USB_MODE_FIRMWARE, 0, NULL, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    RtSleep(10);
    RtWrite(Adapter, RT_H2M_MAILBOX_CSR, 0);

    for (i = 0; i < RT_BUSY_COUNT; i++)
    {
        Value = RtRead(Adapter, RT_PBF_SYS_CTRL);
        if (Value != MAXULONG && (Value & RT_PBF_SYS_CTRL_READY))
            break;
        RtSleep(1);
    }

    if (i == RT_BUSY_COUNT)
        return STATUS_DEVICE_NOT_READY;

    RtDisableWpdma(Adapter);
    RtWrite(Adapter, RT_H2M_BBP_AGENT, 0);
    RtWrite(Adapter, RT_H2M_MAILBOX_CSR, 0);
    RtWrite(Adapter, RT_H2M_INT_SRC, 0);
    RtMcuRequest(Adapter, RT_MCU_BOOT_SIGNAL, 0, 0, 0);
    RtSleep(1);
    return STATUS_SUCCESS;
}

static
USHORT
RtEeprom(
    _In_ PRT_ADAPTER Adapter,
    _In_ ULONG Word)
{
    return Word < RT_EEPROM_WORDS ? Adapter->Eeprom[Word] : 0xffff;
}

static
CHAR
RtEepromOffset(
    _In_ UCHAR Value)
{
    if (Value == 0xff || Value == 0x00)
        return 0;

    if ((CHAR)Value < -10 || (CHAR)Value > 10)
        return 0;

    return (CHAR)Value;
}

static
NTSTATUS
RtReadEeprom(
    _In_ PRT_ADAPTER Adapter)
{
    USHORT Conf0, Conf1, Word;
    NTSTATUS Status;
    ULONG i;

    Status = RtVendorRequest(Adapter,
                             RT_USB_EEPROM_READ,
                             TRUE,
                             0,
                             0,
                             Adapter->Eeprom,
                             sizeof(Adapter->Eeprom));
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < 3; i++)
    {
        Word = RtEeprom(Adapter, RT_EEPROM_MAC_ADDR + i);
        Adapter->PermanentAddress[i * 2] = (UCHAR)Word;
        Adapter->PermanentAddress[i * 2 + 1] = (UCHAR)(Word >> 8);
    }

    if ((Adapter->PermanentAddress[0] & 1) ||
        RtlCompareMemory(Adapter->PermanentAddress, "\0\0\0\0\0\0", RT_ADDRESS_LENGTH) == RT_ADDRESS_LENGTH)
    {
        Adapter->PermanentAddress[0] = 0x02;
        Adapter->PermanentAddress[1] = 0x52;
        Adapter->PermanentAddress[2] = 0x54;
        Adapter->PermanentAddress[3] = 0x32;
        Adapter->PermanentAddress[4] = (UCHAR)KeQueryInterruptTime();
        Adapter->PermanentAddress[5] = (UCHAR)(KeQueryInterruptTime() >> 8);
    }

    Conf0 = RtEeprom(Adapter, RT_EEPROM_NIC_CONF0);
    if (Conf0 == 0xffff || Conf0 == 0)
        Conf0 = 0x0212;

    Adapter->RxChains = (UCHAR)(Conf0 & 0x000f);
    Adapter->TxChains = (UCHAR)((Conf0 & 0x00f0) >> 4);
    Adapter->RfType = (Conf0 & 0x0f00) >> 8;
    if (!Adapter->RxChains || Adapter->RxChains > 3)
        Adapter->RxChains = 2;
    if (!Adapter->TxChains || Adapter->TxChains > 2)
        Adapter->TxChains = 1;

    if (Adapter->RfType != RT_RF2820 && Adapter->RfType != RT_RF2850 &&
        Adapter->RfType != RT_RF2720 && Adapter->RfType != RT_RF2750)
    {
        DPRINT1("RT2800USB: unsupported RF 0x%04x\n", Adapter->RfType);
        return STATUS_NOT_SUPPORTED;
    }

    Adapter->DualBand = Adapter->RfType == RT_RF2850 || Adapter->RfType == RT_RF2750;

    Conf1 = RtEeprom(Adapter, RT_EEPROM_NIC_CONF1);
    if (Conf1 == 0xffff)
        Conf1 = 0;
    Adapter->ExternalLnaBg = (Conf1 & 0x0004) != 0;
    Adapter->ExternalLnaA = (Conf1 & 0x0008) != 0;

    Word = RtEeprom(Adapter, RT_EEPROM_FREQ);
    Adapter->FrequencyOffset = (UCHAR)(Word & 0xff);
    if (Adapter->FrequencyOffset == 0xff)
        Adapter->FrequencyOffset = 0;

    Word = RtEeprom(Adapter, RT_EEPROM_LNA);
    Adapter->LnaGainBg = (UCHAR)(Word & 0xff) == 0xff ? 0 : (CHAR)(Word & 0xff);
    Adapter->LnaGainA = (UCHAR)(Word >> 8) == 0xff ? 0 : (CHAR)(Word >> 8);

    Word = RtEeprom(Adapter, RT_EEPROM_RSSI_BG);
    Adapter->RssiOffsetBg[0] = RtEepromOffset((UCHAR)Word);
    Adapter->RssiOffsetBg[1] = RtEepromOffset((UCHAR)(Word >> 8));
    Word = RtEeprom(Adapter, RT_EEPROM_RSSI_BG2);
    Adapter->RssiOffsetBg[2] = RtEepromOffset((UCHAR)Word);
    Word = RtEeprom(Adapter, RT_EEPROM_RSSI_A);
    Adapter->RssiOffsetA[0] = RtEepromOffset((UCHAR)Word);
    Adapter->RssiOffsetA[1] = RtEepromOffset((UCHAR)(Word >> 8));
    Word = RtEeprom(Adapter, RT_EEPROM_RSSI_A2);
    Adapter->RssiOffsetA[2] = RtEepromOffset((UCHAR)Word);

    DPRINT1("RT2800USB: chip %04lx rev %04lx RF %u %uT%uR MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            Adapter->ChipId,
            Adapter->ChipRevision,
            Adapter->RfType,
            Adapter->TxChains,
            Adapter->RxChains,
            Adapter->PermanentAddress[0], Adapter->PermanentAddress[1], Adapter->PermanentAddress[2],
            Adapter->PermanentAddress[3], Adapter->PermanentAddress[4], Adapter->PermanentAddress[5]);
    return STATUS_SUCCESS;
}

static
VOID
RtUsbResetRegisters(
    _In_ PRT_ADAPTER Adapter)
{
    RtWaitCsrReady(Adapter);
    RtWrite(Adapter, RT_PBF_SYS_CTRL, RtRead(Adapter, RT_PBF_SYS_CTRL) & ~0x00002000);
    RtWrite(Adapter, RT_MAC_SYS_CTRL, RT_MAC_SYS_CTRL_RESET_CSR | RT_MAC_SYS_CTRL_RESET_BBP);
    RtWrite(Adapter, RT_USB_DMA_CFG, 0);
    RtVendorRequest(Adapter, RT_USB_DEVICE_MODE, FALSE, RT_USB_MODE_RESET, 0, NULL, 0);
    RtWrite(Adapter, RT_MAC_SYS_CTRL, 0);
}

static
VOID
RtWriteProtection(
    _In_ PRT_ADAPTER Adapter,
    _In_ USHORT Offset,
    _In_ ULONG Rate,
    _In_ BOOLEAN Allow40,
    _In_ BOOLEAN RtsThreshold)
{
    ULONG Value = RtRead(Adapter, Offset);

    Value = RtSetBits(Value, 0x0000ffff, Rate);
    Value = RtSetBits(Value, 0x00030000, 0);
    Value = RtSetBits(Value, 0x00040000, 1);
    Value = RtSetBits(Value, 0x00100000, 1);
    Value = RtSetBits(Value, 0x00200000, 1);
    Value = RtSetBits(Value, 0x00400000, 1);
    Value = RtSetBits(Value, 0x00800000, Allow40);
    Value = RtSetBits(Value, 0x01000000, 1);
    Value = RtSetBits(Value, 0x02000000, Allow40);
    Value = RtSetBits(Value, 0x04000000, RtsThreshold);
    RtWrite(Adapter, Offset, Value);
}

static
VOID
RtInitRegisters(
    _In_ PRT_ADAPTER Adapter)
{
    ULONG Value, i;
    static const UCHAR ZeroEntry[32] = { 0 };
    static const UCHAR EmptyWcid[8] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    RtDisableWpdma(Adapter);
    RtUsbResetRegisters(Adapter);

    RtWrite(Adapter, RT_LEGACY_BASIC_RATE, 0x0000013f);
    RtWrite(Adapter, RT_HT_BASIC_RATE, 0x00008003);
    RtWrite(Adapter, RT_MAC_SYS_CTRL, 0);

    Value = RtRead(Adapter, RT_BCN_TIME_CFG);
    Value = RtSetBits(Value, RT_BCN_TIME_INTERVAL_MASK, 1600);
    Value &= ~(RT_BCN_TIME_TSF_TICKING | RT_BCN_TIME_TSF_SYNC_MASK | RT_BCN_TIME_TBTT_ENABLE |
               RT_BCN_TIME_BEACON_GEN | RT_BCN_TIME_COMPENSATE_MASK);
    RtWrite(Adapter, RT_BCN_TIME_CFG, Value);

    RtSetRxFilter(Adapter, TRUE);

    Value = RtRead(Adapter, RT_BKOFF_SLOT_CFG);
    Value = RtSetBits(Value, 0x000000ff, 9);
    Value = RtSetBits(Value, 0x0000ff00, 2);
    RtWrite(Adapter, RT_BKOFF_SLOT_CFG, Value);

    RtWrite(Adapter, RT_TX_SW_CFG0, 0);
    RtWrite(Adapter, RT_TX_SW_CFG1, 0x00080606);

    Value = RtRead(Adapter, RT_TX_LINK_CFG);
    Value = RtSetBits(Value, 0x000000ff, 32);
    Value &= ~(0x00000100 | 0x00000200 | 0x00000400 | 0x00000800 | 0x00ff0000 | 0xff000000);
    Value |= 0x00001000;
    RtWrite(Adapter, RT_TX_LINK_CFG, Value);

    Value = RtRead(Adapter, RT_TX_TIMEOUT_CFG);
    Value = RtSetBits(Value, 0x000000f0, 9);
    Value = RtSetBits(Value, 0x0000ff00, 32);
    Value = RtSetBits(Value, 0x00ff0000, 10);
    RtWrite(Adapter, RT_TX_TIMEOUT_CFG, Value);

    Value = RtRead(Adapter, RT_MAX_LEN_CFG);
    Value = RtSetBits(Value, 0x00000fff, 3840);
    Value = RtSetBits(Value, 0x00003000, Adapter->ChipId == 0x2860 && Adapter->ChipRevision >= 0x0103 ? 2 : 1);
    Value &= ~(0x0000c000 | 0x000f0000);
    RtWrite(Adapter, RT_MAX_LEN_CFG, Value);

    Value = RtRead(Adapter, RT_LED_CFG);
    Value = RtSetBits(Value, 0x000000ff, 70);
    Value = RtSetBits(Value, 0x0000ff00, 30);
    Value = RtSetBits(Value, 0x003f0000, 3);
    Value = RtSetBits(Value, 0x03000000, 3);
    Value = RtSetBits(Value, 0x0c000000, 3);
    Value = RtSetBits(Value, 0x30000000, 3);
    Value = RtSetBits(Value, 0x40000000, 1);
    RtWrite(Adapter, RT_LED_CFG, Value);

    RtWrite(Adapter, RT_PBF_MAX_PCNT, 0x1f3fbf9f);

    Value = RtRead(Adapter, RT_TX_RTY_CFG);
    Value = RtSetBits(Value, 0x000000ff, 15);
    Value = RtSetBits(Value, 0x0000ff00, 31);
    Value = RtSetBits(Value, 0x0fff0000, 2000);
    Value &= ~(0x10000000 | 0x20000000);
    Value |= 0x40000000;
    RtWrite(Adapter, RT_TX_RTY_CFG, Value);

    Value = RtRead(Adapter, RT_AUTO_RSP_CFG);
    Value &= ~(0x00000004 | 0x00000008 | 0x00000040 | 0x00000080);
    Value |= 0x00000001 | 0x00000002 | 0x00000010;
    RtWrite(Adapter, RT_AUTO_RSP_CFG, Value);

    RtWriteProtection(Adapter, RT_CCK_PROT_CFG, 3, FALSE, TRUE);
    RtWriteProtection(Adapter, RT_OFDM_PROT_CFG, 3, FALSE, TRUE);
    RtWriteProtection(Adapter, RT_MM20_PROT_CFG, 0x4004, FALSE, FALSE);
    RtWriteProtection(Adapter, RT_MM40_PROT_CFG, 0x4084, TRUE, FALSE);
    RtWriteProtection(Adapter, RT_GF20_PROT_CFG, 0x4004, FALSE, FALSE);
    RtWriteProtection(Adapter, RT_GF40_PROT_CFG, 0x4084, TRUE, FALSE);

    RtWrite(Adapter, RT_PBF_CFG, 0x00f40006);

    Value = RtRead(Adapter, RT_WPDMA_GLO_CFG);
    Value &= ~(RT_WPDMA_ENABLE_TX_DMA | RT_WPDMA_TX_DMA_BUSY | RT_WPDMA_ENABLE_RX_DMA |
               RT_WPDMA_RX_DMA_BUSY | RT_WPDMA_TX_WRITEBACK_DONE | RT_WPDMA_BIG_ENDIAN |
               RT_WPDMA_RX_HDR_SCATTER | RT_WPDMA_HDR_SEG_LEN);
    Value = RtSetBits(Value, RT_WPDMA_BURST_SIZE_MASK, 3);
    RtWrite(Adapter, RT_WPDMA_GLO_CFG, Value);

    Value = RtRead(Adapter, RT_TXOP_CTRL_CFG);
    Value |= 0x00000001 | 0x00000002 | 0x00000004 | 0x00000008 | 0x00000010 | 0x00000020;
    Value &= ~(0x00000040 | 0x00000080 | 0x000f0000);
    Value = RtSetBits(Value, 0x0000ff00, 88);
    RtWrite(Adapter, RT_TXOP_CTRL_CFG, Value);

    RtWrite(Adapter, RT_TXOP_HLDR_ET, 0x00000002);

    Value = RtRead(Adapter, RT_TX_RTS_CFG);
    Value = RtSetBits(Value, 0x000000ff, 32);
    Value = RtSetBits(Value, 0x00ffff00, 2347);
    Value &= ~0x01000000;
    RtWrite(Adapter, RT_TX_RTS_CFG, Value);

    RtWrite(Adapter, RT_EXP_ACK_TIME, 0x002400ca);

    Value = RtRead(Adapter, RT_XIFS_TIME_CFG);
    Value = RtSetBits(Value, 0x000000ff, 16);
    Value = RtSetBits(Value, 0x0000ff00, 16);
    Value = RtSetBits(Value, 0x000f0000, 4);
    Value = RtSetBits(Value, 0x1ff00000, 314);
    Value |= 0x20000000;
    RtWrite(Adapter, RT_XIFS_TIME_CFG, Value);

    RtWrite(Adapter, RT_PWR_PIN_CFG, 0x00000003);

    for (i = 0; i < 4; i++)
        RtWrite(Adapter, (USHORT)(RT_SHARED_KEY_MODE_BASE + i * 4), 0);

    for (i = 0; i < 256; i++)
    {
        RtWriteBlock(Adapter, (USHORT)(RT_MAC_WCID_BASE + i * 8), EmptyWcid, sizeof(EmptyWcid));
        RtWrite(Adapter, (USHORT)(RT_MAC_WCID_ATTR_BASE + i * 4), 0);
        RtWriteBlock(Adapter, (USHORT)(RT_MAC_IVEIV_BASE + i * 8), ZeroEntry, 8);
    }

    for (i = 0; i < 8; i++)
        RtWriteBlock(Adapter, (USHORT)(RT_HW_BEACON_BASE0 + i * 0x200), ZeroEntry, sizeof(ZeroEntry));

    Value = RtRead(Adapter, RT_US_CYC_CNT);
    RtWrite(Adapter, RT_US_CYC_CNT, RtSetBits(Value, 0x000000ff, 30));

    RtWrite(Adapter, RT_HT_FBK_CFG0, 0x65432100);
    RtWrite(Adapter, RT_HT_FBK_CFG1, 0xedcba988);
    RtWrite(Adapter, RT_LG_FBK_CFG0, 0xedcba988);
    RtWrite(Adapter, RT_LG_FBK_CFG1, (RtRead(Adapter, RT_LG_FBK_CFG1) & 0xffff0000) | 0x00002100);

    RtWrite(Adapter, RT_AMPDU_BA_WINSIZE, RtRead(Adapter, RT_AMPDU_BA_WINSIZE) & ~0x0000003f);

    RtRead(Adapter, RT_RX_STA_CNT0);
    RtRead(Adapter, RT_RX_STA_CNT1);
    RtRead(Adapter, RT_RX_STA_CNT2);
    RtRead(Adapter, RT_TX_STA_CNT0);
    RtRead(Adapter, RT_TX_STA_CNT1);
    RtRead(Adapter, RT_TX_STA_CNT2);

    Value = RtRead(Adapter, RT_INT_TIMER_CFG);
    RtWrite(Adapter, RT_INT_TIMER_CFG, RtSetBits(Value, 0x0000ffff, 6 << 4));

    RtWrite(Adapter, RT_CH_TIME_CFG, RtRead(Adapter, RT_CH_TIME_CFG) | 0x1f);

    RtWrite(Adapter, RT_EDCA_AC0_CFG + 0, (2 << 8) | (2 << 12) | (3 << 16) | 47);
    RtWrite(Adapter, RT_EDCA_AC0_CFG + 4, (2 << 8) | (3 << 12) | (4 << 16) | 94);
    RtWrite(Adapter, RT_EDCA_AC0_CFG + 8, (3 << 8) | (4 << 12) | (10 << 16));
    RtWrite(Adapter, RT_EDCA_AC0_CFG + 12, (7 << 8) | (4 << 12) | (10 << 16));
}

static
NTSTATUS
RtWaitBbpReady(
    _In_ PRT_ADAPTER Adapter)
{
    UCHAR Value;
    ULONG i;

    RtWrite(Adapter, RT_H2M_BBP_AGENT, 0);
    RtWrite(Adapter, RT_H2M_MAILBOX_CSR, 0);
    RtSleep(1);

    for (i = 0; i < RT_BUSY_COUNT; i++)
    {
        Value = RtBbpRead(Adapter, 0);
        if (Value != 0xff && Value != 0x00)
            return STATUS_SUCCESS;
        RtStallUs(RT_BUSY_DELAY_US);
    }

    return STATUS_DEVICE_NOT_READY;
}

static
VOID
RtInitBbp(
    _In_ PRT_ADAPTER Adapter)
{
    USHORT Word;
    ULONG i;

    RtBbpWrite(Adapter, 65, 0x2c);
    RtBbpWrite(Adapter, 66, 0x38);
    if (Adapter->ChipId == 0x2860 && Adapter->ChipRevision == RT_REV_RT2860C)
    {
        RtBbpWrite(Adapter, 69, 0x16);
        RtBbpWrite(Adapter, 73, 0x12);
    }
    else
    {
        RtBbpWrite(Adapter, 69, 0x12);
        RtBbpWrite(Adapter, 73, 0x10);
    }
    RtBbpWrite(Adapter, 70, 0x0a);
    RtBbpWrite(Adapter, 81, 0x37);
    RtBbpWrite(Adapter, 82, 0x62);
    RtBbpWrite(Adapter, 83, 0x6a);
    RtBbpWrite(Adapter, 84, Adapter->ChipId == 0x2860 && Adapter->ChipRevision == RT_REV_RT2860D ? 0x19 : 0x99);
    RtBbpWrite(Adapter, 86, 0x00);
    RtBbpWrite(Adapter, 91, 0x04);
    RtBbpWrite(Adapter, 92, 0x00);
    RtBbpWrite(Adapter, 103, 0x00);
    RtBbpWrite(Adapter, 105, 0x05);
    RtBbpWrite(Adapter, 106, 0x35);

    for (i = 0; i < RT_EEPROM_BBP_SIZE; i++)
    {
        Word = RtEeprom(Adapter, RT_EEPROM_BBP_START + i);
        if (Word != 0xffff && Word != 0)
            RtBbpWrite(Adapter, (UCHAR)(Word >> 8), (UCHAR)Word);
    }
}

static
VOID
RtConfigureAntenna(
    _In_ PRT_ADAPTER Adapter)
{
    UCHAR R1, R3;

    R1 = RtBbpRead(Adapter, 1);
    R3 = RtBbpRead(Adapter, 3);

    R1 &= ~0x18;
    if (Adapter->TxChains == 2)
        R1 |= 0x10;

    R3 &= ~0x18;
    if (Adapter->RxChains == 3)
        R3 |= 0x10;
    else if (Adapter->RxChains == 2)
        R3 |= 0x08;

    RtBbpWrite(Adapter, 1, R1);
    RtBbpWrite(Adapter, 3, R3);
}

static
VOID
RtConfigureTxPower(
    _In_ PRT_ADAPTER Adapter)
{
    ULONG i, Value;

    for (i = 0; i < 5; i++)
    {
        Value = RtEeprom(Adapter, RT_EEPROM_TXPOWER_BYRATE + i * 2) |
                ((ULONG)RtEeprom(Adapter, RT_EEPROM_TXPOWER_BYRATE + i * 2 + 1) << 16);
        if (Value == MAXULONG)
            Value = 0;
        RtWrite(Adapter, (USHORT)(RT_TX_PWR_CFG_0 + i * 4), Value);
    }
}

static
UCHAR
RtChannelPower(
    _In_ PRT_ADAPTER Adapter,
    _In_ ULONG Channel,
    _In_ ULONG Chain)
{
    ULONG Base, Index;
    USHORT Word;
    CHAR Power;

    if (Channel <= 14)
    {
        Base = Chain ? RT_EEPROM_TXPOWER_BG2 : RT_EEPROM_TXPOWER_BG1;
        Index = Channel - 1;
    }
    else
    {
        Base = Chain ? RT_EEPROM_TXPOWER_A2 : RT_EEPROM_TXPOWER_A1;
        for (Index = 0; Index < RTL_NUMBER_OF(RtRfChannels) && RtRfChannels[Index].Channel != Channel; Index++)
        {
        }
        Index -= 14;
    }

    Word = RtEeprom(Adapter, Base + Index / 2);
    Power = (CHAR)((Index & 1) ? (Word >> 8) : Word);

    if (Channel <= 14)
        Power = (CHAR)min(max(Power, 0), 31);
    else
        Power = (CHAR)min(max(Power, -7), 15);

    return (UCHAR)Power;
}

VOID
RtSetChannel(
    _In_ PRT_ADAPTER Adapter,
    _In_ ULONG Channel)
{
    const RT_RF_CHANNEL* Entry = NULL;
    ULONG Rf1, Rf2, Rf3, Rf4, TxPin, Value, i;
    CHAR Power0, Power1;
    UCHAR Lna;

    for (i = 0; i < RTL_NUMBER_OF(RtRfChannels); i++)
    {
        if (RtRfChannels[i].Channel == Channel)
        {
            Entry = &RtRfChannels[i];
            break;
        }
    }

    if (!Entry || (Channel > 14 && !Adapter->DualBand))
        return;

    Rf1 = Entry->Rf1;
    Rf2 = Entry->Rf2;
    Rf3 = Entry->Rf3;
    Rf4 = Entry->Rf4;

    Rf4 = RtSetBits(Rf4, 0x001f8000, Adapter->FrequencyOffset);

    if (Adapter->TxChains == 1)
        Rf2 |= 0x00004000;

    if (Adapter->RxChains == 1)
        Rf2 |= 0x00020000 | 0x00000040;
    else if (Adapter->RxChains == 2)
        Rf2 |= 0x00000040;

    Power0 = (CHAR)RtChannelPower(Adapter, Channel, 0);
    Power1 = (CHAR)RtChannelPower(Adapter, Channel, 1);

    if (Channel > 14)
    {
        Rf3 = RtSetBits(Rf3, 0x00000200, Power0 >= 0);
        if (Power0 < 0)
            Power0 += 7;
        Rf3 = RtSetBits(Rf3, 0x00003c00, (ULONG)Power0);
        Rf4 = RtSetBits(Rf4, 0x00000040, Power1 >= 0);
        if (Power1 < 0)
            Power1 += 7;
        Rf4 = RtSetBits(Rf4, 0x00000780, (ULONG)Power1);
    }
    else
    {
        Rf3 = RtSetBits(Rf3, 0x00003e00, (ULONG)Power0);
        Rf4 = RtSetBits(Rf4, 0x000007c0, (ULONG)Power1);
    }

    Rf4 &= ~0x00200000;

    RtRfWrite(Adapter, Rf1);
    RtRfWrite(Adapter, Rf2);
    RtRfWrite(Adapter, Rf3 & ~0x00000004);
    RtRfWrite(Adapter, Rf4);
    RtStallUs(200);
    RtRfWrite(Adapter, Rf1);
    RtRfWrite(Adapter, Rf2);
    RtRfWrite(Adapter, Rf3 | 0x00000004);
    RtRfWrite(Adapter, Rf4);
    RtStallUs(200);
    RtRfWrite(Adapter, Rf1);
    RtRfWrite(Adapter, Rf2);
    RtRfWrite(Adapter, Rf3 & ~0x00000004);
    RtRfWrite(Adapter, Rf4);

    Lna = (UCHAR)(Channel <= 14 ? Adapter->LnaGainBg : Adapter->LnaGainA);
    RtBbpWrite(Adapter, 62, (UCHAR)(0x37 - Lna));
    RtBbpWrite(Adapter, 63, (UCHAR)(0x37 - Lna));
    RtBbpWrite(Adapter, 64, (UCHAR)(0x37 - Lna));
    RtBbpWrite(Adapter, 86, 0);

    if (Channel <= 14)
    {
        if (Adapter->ExternalLnaBg)
        {
            RtBbpWrite(Adapter, 82, 0x62);
            RtBbpWrite(Adapter, 75, 0x46);
        }
        else
        {
            RtBbpWrite(Adapter, 82, 0x84);
            RtBbpWrite(Adapter, 75, 0x50);
        }
    }
    else
    {
        RtBbpWrite(Adapter, 82, 0xf2);
        RtBbpWrite(Adapter, 75, Adapter->ExternalLnaA ? 0x46 : 0x50);
    }

    Value = RtRead(Adapter, RT_TX_BAND_CFG);
    Value &= ~(RT_TX_BAND_HT40_MINUS | RT_TX_BAND_A | RT_TX_BAND_BG);
    Value |= Channel > 14 ? RT_TX_BAND_A : RT_TX_BAND_BG;
    RtWrite(Adapter, RT_TX_BAND_CFG, Value);

    TxPin = RT_TX_PIN_RFTR_EN | RT_TX_PIN_TRSW_EN;
    if (Adapter->TxChains >= 2)
        TxPin |= Channel > 14 ? RT_TX_PIN_PA_PE_A1_EN : RT_TX_PIN_PA_PE_G1_EN;
    TxPin |= Channel > 14 ? RT_TX_PIN_PA_PE_A0_EN : RT_TX_PIN_PA_PE_G0_EN;
    if (Adapter->RxChains >= 3)
        TxPin |= RT_TX_PIN_LNA_PE_A2_EN | RT_TX_PIN_LNA_PE_G2_EN;
    if (Adapter->RxChains >= 2)
        TxPin |= RT_TX_PIN_LNA_PE_A1_EN | RT_TX_PIN_LNA_PE_G1_EN;
    TxPin |= RT_TX_PIN_LNA_PE_A0_EN | RT_TX_PIN_LNA_PE_G0_EN;
    RtWrite(Adapter, RT_TX_PIN_CFG, TxPin);

    RtBbpWrite(Adapter, 4, RtBbpRead(Adapter, 4) & ~0x18);
    RtBbpWrite(Adapter, 3, RtBbpRead(Adapter, 3) & ~0x20);

    if (Adapter->ChipId == 0x2860 && Adapter->ChipRevision == RT_REV_RT2860C)
    {
        RtBbpWrite(Adapter, 69, 0x16);
        RtBbpWrite(Adapter, 70, 0x08);
        RtBbpWrite(Adapter, 73, 0x11);
    }

    RtSleep(1);
    RtRead(Adapter, RT_CH_IDLE_STA);
    RtRead(Adapter, RT_CH_BUSY_STA);
    RtRead(Adapter, RT_CH_BUSY_STA_SEC);

    Adapter->CurrentChannel = Channel;
}

VOID
RtSetRxFilter(
    _In_ PRT_ADAPTER Adapter,
    _In_ BOOLEAN Scanning)
{
    ULONG Value = RtRead(Adapter, RT_RX_FILTER_CFG);

    UNREFERENCED_PARAMETER(Scanning);

    Value |= RT_RX_FILTER_DROP_CRC_ERROR |
             RT_RX_FILTER_DROP_PHY_ERROR |
             RT_RX_FILTER_DROP_NOT_TO_ME |
             RT_RX_FILTER_DROP_VER_ERROR |
             RT_RX_FILTER_DROP_DUPLICATE |
             RT_RX_FILTER_DROP_CONTROL;
    Value &= ~(RT_RX_FILTER_DROP_NOT_MY_BSSID |
               RT_RX_FILTER_DROP_MULTICAST |
               RT_RX_FILTER_DROP_BROADCAST |
               RT_RX_FILTER_DROP_BA);
    RtWrite(Adapter, RT_RX_FILTER_CFG, Value);
}

static
VOID
RtSetMacAddress(
    _In_ PRT_ADAPTER Adapter)
{
    ULONG Words[2];

    Words[0] = Adapter->CurrentAddress[0] |
               ((ULONG)Adapter->CurrentAddress[1] << 8) |
               ((ULONG)Adapter->CurrentAddress[2] << 16) |
               ((ULONG)Adapter->CurrentAddress[3] << 24);
    Words[1] = Adapter->CurrentAddress[4] |
               ((ULONG)Adapter->CurrentAddress[5] << 8) |
               0x00ff0000;
    RtWrite(Adapter, RT_MAC_ADDR_DW0, Words[0]);
    RtWrite(Adapter, RT_MAC_ADDR_DW1, Words[1]);
}

VOID
RtSetBssid(
    _In_ PRT_ADAPTER Adapter,
    _In_reads_bytes_opt_(RT_ADDRESS_LENGTH) const UCHAR* Bssid)
{
    static const UCHAR Zero[RT_ADDRESS_LENGTH] = { 0 };
    ULONG Words[2];

    if (!Bssid)
        Bssid = Zero;

    Words[0] = Bssid[0] | ((ULONG)Bssid[1] << 8) | ((ULONG)Bssid[2] << 16) | ((ULONG)Bssid[3] << 24);
    Words[1] = Bssid[4] | ((ULONG)Bssid[5] << 8);
    if (Bssid != Zero)
        Words[1] |= 3u << 16;

    RtWrite(Adapter, RT_MAC_BSSID_DW0, Words[0]);
    RtWrite(Adapter, RT_MAC_BSSID_DW1, Words[1]);
}

VOID
RtSetAssociated(
    _In_ PRT_ADAPTER Adapter,
    _In_ BOOLEAN Associated,
    _In_ USHORT BeaconInterval)
{
    ULONG Value = RtRead(Adapter, RT_BCN_TIME_CFG);

    Value &= ~(RT_BCN_TIME_TSF_SYNC_MASK | RT_BCN_TIME_TSF_TICKING | RT_BCN_TIME_TBTT_ENABLE);
    if (Associated)
    {
        Value = RtSetBits(Value, RT_BCN_TIME_INTERVAL_MASK, (ULONG)(BeaconInterval ? BeaconInterval : 100) * 16);
        Value |= (1u << RT_BCN_TIME_TSF_SYNC_SHIFT) | RT_BCN_TIME_TSF_TICKING | RT_BCN_TIME_TBTT_ENABLE;
    }
    RtWrite(Adapter, RT_BCN_TIME_CFG, Value);

    Value = RtRead(Adapter, RT_TBTT_SYNC_CFG);
    Value = RtSetBits(Value, 0x00f00000, 4);
    Value = RtSetBits(Value, 0x000f0000, 2);
    Value = RtSetBits(Value, 0x0000ff00, 32);
    Value = RtSetBits(Value, 0x000000ff, 16);
    RtWrite(Adapter, RT_TBTT_SYNC_CFG, Value);
}

static
VOID
RtWriteWcid(
    _In_ PRT_ADAPTER Adapter,
    _In_ ULONG Wcid,
    _In_reads_bytes_opt_(RT_ADDRESS_LENGTH) const UCHAR* Address)
{
    UCHAR Entry[8];

    RtlFillMemory(Entry, sizeof(Entry), 0xff);
    if (Address)
        RtlCopyMemory(Entry, Address, RT_ADDRESS_LENGTH);
    RtWriteBlock(Adapter, (USHORT)(RT_MAC_WCID_BASE + Wcid * 8), Entry, sizeof(Entry));
}

NTSTATUS
RtInstallKey(
    _In_ PRT_ADAPTER Adapter,
    _In_ BOOLEAN Pairwise,
    _In_ ULONG KeyIndex,
    _In_ ULONG Cipher,
    _In_reads_bytes_(KeyLength) const UCHAR* Key,
    _In_ ULONG KeyLength,
    _In_reads_bytes_opt_(RT_ADDRESS_LENGTH) const UCHAR* Peer)
{
    UCHAR Entry[32], IvEiv[8];
    ULONG Wcid, Attribute, Mode, Shift;

    if (KeyIndex > 3 || KeyLength > sizeof(Entry))
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Entry, sizeof(Entry));
    RtlCopyMemory(Entry, Key, KeyLength);
    if (Cipher == RT_CIPHER_TKIP && KeyLength == 32)
    {
        RtlCopyMemory(Entry + 16, Key + 16, 8);
        RtlCopyMemory(Entry + 24, Key + 24, 8);
    }

    if (Pairwise)
    {
        Wcid = RT_AP_WCID;
        RtWriteBlock(Adapter, (USHORT)(RT_PAIRWISE_KEY_BASE + Wcid * 32), Entry, sizeof(Entry));
        RtWriteWcid(Adapter, Wcid, Peer);
    }
    else
    {
        Wcid = KeyIndex;
        RtWriteBlock(Adapter, (USHORT)(RT_SHARED_KEY_BASE + Wcid * 32), Entry, sizeof(Entry));
        Shift = 4 * (Wcid % 8);
        Mode = RtRead(Adapter, (USHORT)(RT_SHARED_KEY_MODE_BASE + (Wcid / 8) * 4));
        Mode = (Mode & ~(7u << Shift)) | ((Cipher & 7) << Shift);
        RtWrite(Adapter, (USHORT)(RT_SHARED_KEY_MODE_BASE + (Wcid / 8) * 4), Mode);
        RtWriteWcid(Adapter, Wcid, NULL);
    }

    Attribute = (Pairwise ? RT_WCID_ATTR_KEYTAB : 0) |
                ((Cipher & 7) << RT_WCID_ATTR_CIPHER_SHIFT) |
                ((Cipher & 7) << RT_WCID_ATTR_RX_WIUDF_SHIFT);
    RtWrite(Adapter, (USHORT)(RT_MAC_WCID_ATTR_BASE + Wcid * 4), Attribute);

    RtlZeroMemory(IvEiv, sizeof(IvEiv));
    if (Cipher == RT_CIPHER_TKIP || Cipher == RT_CIPHER_AES)
        IvEiv[3] |= 0x20;
    IvEiv[3] |= (UCHAR)(KeyIndex << 6);
    RtWriteBlock(Adapter, (USHORT)(RT_MAC_IVEIV_BASE + Wcid * 8), IvEiv, sizeof(IvEiv));

    if (Pairwise)
        Adapter->PairwiseKeyInstalled = TRUE;

    return STATUS_SUCCESS;
}

VOID
RtRemoveKeys(
    _In_ PRT_ADAPTER Adapter)
{
    static const UCHAR Zero[8] = { 0 };
    ULONG i;

    Adapter->PairwiseKeyInstalled = FALSE;
    Adapter->MgmtKeyValid = FALSE;
    Adapter->IgtkValid = FALSE;
    RtlSecureZeroMemory(Adapter->MgmtTk, sizeof(Adapter->MgmtTk));
    RtlSecureZeroMemory(Adapter->Igtk, sizeof(Adapter->Igtk));
    RtlZeroMemory(Adapter->GroupKeyValid, sizeof(Adapter->GroupKeyValid));
    RtlSecureZeroMemory(Adapter->GroupKey, sizeof(Adapter->GroupKey));

    for (i = 0; i < 4; i++)
    {
        RtWrite(Adapter, (USHORT)(RT_MAC_WCID_ATTR_BASE + i * 4), 0);
        RtWriteWcid(Adapter, i, NULL);
    }
    RtWrite(Adapter, RT_SHARED_KEY_MODE_BASE, 0);

    RtWrite(Adapter, (USHORT)(RT_MAC_WCID_ATTR_BASE + RT_AP_WCID * 4), 0);
    RtWriteBlock(Adapter, (USHORT)(RT_MAC_IVEIV_BASE + RT_AP_WCID * 8), Zero, sizeof(Zero));
}

LONG
RtRssiFromRxwi(
    _In_ PRT_ADAPTER Adapter,
    _In_ ULONG Rxwi2,
    _In_ ULONG Channel)
{
    const CHAR* Offsets = Channel <= 14 ? Adapter->RssiOffsetBg : Adapter->RssiOffsetA;
    CHAR Lna = Channel <= 14 ? Adapter->LnaGainBg : Adapter->LnaGainA;
    LONG Best = -128, Rssi;
    CHAR Raw;
    ULONG i;

    for (i = 0; i < 3; i++)
    {
        Raw = (CHAR)(Rxwi2 >> (8 * i));
        if (!Raw)
            continue;
        Rssi = -12 - Offsets[i] - Lna - Raw;
        if (Rssi > Best)
            Best = Rssi;
    }

    return Best;
}

NTSTATUS
RtHardwareInitialize(
    _In_ PRT_ADAPTER Adapter)
{
    NTSTATUS Status;
    USHORT Word;
    ULONG Value;

    Status = RtWaitCsrReady(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtReadEeprom(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlCopyMemory(Adapter->CurrentAddress, Adapter->PermanentAddress, RT_ADDRESS_LENGTH);

    Status = RtLoadFirmware(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RT2800USB: firmware load failed 0x%08lx\n", Status);
        return Status;
    }

    RtMcuRequest(Adapter, RT_MCU_WAKEUP, 0xff, 0, 2);
    RtSleep(1);

    Status = RtWaitWpdmaReady(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    RtInitRegisters(Adapter);

    if (!RtWaitClear(Adapter, RT_MAC_STATUS_CFG, RT_MAC_STATUS_BBP_RF_BUSY, NULL))
        return STATUS_DEVICE_NOT_READY;

    RtWrite(Adapter, RT_H2M_BBP_AGENT, 0);
    RtWrite(Adapter, RT_H2M_MAILBOX_CSR, 0);
    RtWrite(Adapter, RT_H2M_INT_SRC, 0);
    RtMcuRequest(Adapter, RT_MCU_BOOT_SIGNAL, 0, 0, 0);
    RtSleep(1);

    Status = RtWaitBbpReady(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    RtInitBbp(Adapter);
    RtConfigureAntenna(Adapter);

    Value = RtRead(Adapter, RT_USB_DMA_CFG);
    Value &= ~(RT_USB_DMA_PHY_CLEAR | RT_USB_DMA_RX_AGG_EN | RT_USB_DMA_AGG_TIMEOUT_MASK | RT_USB_DMA_AGG_LIMIT_MASK);
    Value |= 128 | (((RT_RX_BUFFER_SIZE / 1024) - 3) << RT_USB_DMA_AGG_LIMIT_SHIFT);
    Value |= RT_USB_DMA_RX_AGG_EN | RT_USB_DMA_RX_BULK_EN | RT_USB_DMA_TX_BULK_EN;
    RtWrite(Adapter, RT_USB_DMA_CFG, Value);

    RtWrite(Adapter, RT_MAC_SYS_CTRL, RtRead(Adapter, RT_MAC_SYS_CTRL) | RT_MAC_SYS_CTRL_ENABLE_TX);
    RtStallUs(50);

    Value = RtRead(Adapter, RT_WPDMA_GLO_CFG);
    Value |= RT_WPDMA_ENABLE_TX_DMA | RT_WPDMA_ENABLE_RX_DMA | RT_WPDMA_TX_WRITEBACK_DONE;
    Value = RtSetBits(Value, RT_WPDMA_BURST_SIZE_MASK, 2);
    RtWrite(Adapter, RT_WPDMA_GLO_CFG, Value);

    RtWrite(Adapter, RT_MAC_SYS_CTRL,
            RtRead(Adapter, RT_MAC_SYS_CTRL) | RT_MAC_SYS_CTRL_ENABLE_TX | RT_MAC_SYS_CTRL_ENABLE_RX);

    Word = RtEeprom(Adapter, RT_EEPROM_LED_AG_CONF);
    RtMcuRequest(Adapter, RT_MCU_LED_AG_CONF, 0xff, (UCHAR)Word, (UCHAR)(Word >> 8));
    Word = RtEeprom(Adapter, RT_EEPROM_LED_ACT_CONF);
    RtMcuRequest(Adapter, RT_MCU_LED_ACT_CONF, 0xff, (UCHAR)Word, (UCHAR)(Word >> 8));
    Word = RtEeprom(Adapter, RT_EEPROM_LED_POLARITY);
    RtMcuRequest(Adapter, RT_MCU_LED_POLARITY, 0xff, (UCHAR)Word, (UCHAR)(Word >> 8));

    RtSetMacAddress(Adapter);
    RtSetBssid(Adapter, NULL);
    RtConfigureTxPower(Adapter);
    RtSetChannel(Adapter, 1);
    RtSetRxFilter(Adapter, TRUE);

    Adapter->HardwareReady = TRUE;
    DPRINT1("RT2800USB: radio up, MAC_SYS_CTRL %08lx USB_DMA_CFG %08lx\n",
            RtRead(Adapter, RT_MAC_SYS_CTRL),
            RtRead(Adapter, RT_USB_DMA_CFG));
    return STATUS_SUCCESS;
}

VOID
RtHardwareShutdown(
    _In_ PRT_ADAPTER Adapter)
{
    if (!Adapter->HardwareReady)
        return;

    RtWrite(Adapter, RT_MAC_SYS_CTRL, RtRead(Adapter, RT_MAC_SYS_CTRL) & ~(RT_MAC_SYS_CTRL_ENABLE_TX | RT_MAC_SYS_CTRL_ENABLE_RX));
    RtDisableWpdma(Adapter);
    RtMcuRequest(Adapter, RT_MCU_SLEEP, 0xff, 0xff, 2);
    Adapter->HardwareReady = FALSE;
}
