/*
 * PROJECT:     LiberNT Apple Silicon Audio Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Headphone jack hardware: MCA, ADMAC, NCO and the CS42L84 codec
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <reactos/drivers/fdtdrv.h>
#include <reactos/drivers/applepmgr.h>
#include <reactos/drivers/appledart.h>
#include <reactos/drivers/applegpio.h>
#include <reactos/drivers/applei2c.h>
#include <reactos/drivers/applenco.h>
#include <reactos/drivers/appleadmac.h>
#include <reactos/drivers/applemca.h>
#include "hardware.h"

#define TAG_APPLE_AUDIO_HW          'hAPA'
#define APPLE_AUDIO_LINK_NAME       "Headphone Jack"
#define APPLE_AUDIO_FRONTEND        0
#define APPLE_AUDIO_SLOTS           2
#define APPLE_AUDIO_SLOT_WIDTH      32
#define APPLE_AUDIO_BCLK            (APPLE_AUDIO_SAMPLE_RATE * APPLE_AUDIO_SLOTS * APPLE_AUDIO_SLOT_WIDTH)

#define CS42L84_CHIP_ID             0x42a84
#define CS42L84_DEVID               0x0000
#define CS42L84_FREEZE              0x0006
#define CS42L84_FREEZE_ENGAGE       0x01
#define CS42L84_TSRS_PLUG_INT_MASK  0x0418
#define CS42L84_RS_PLUG             0x01
#define CS42L84_RS_UNPLUG           0x02
#define CS42L84_TS_PLUG             0x04
#define CS42L84_TS_UNPLUG           0x08
#define CS42L84_PLL_LOCK_STATUS     0x040e
#define CS42L84_PLL_LOCKED          0x10
#define CS42L84_CCM_CTL1            0x0600
#define CS42L84_CCM_MCLK_SOURCE_PLL 0x03
#define CS42L84_CCM_MCLK_12_288MHZ  0x08
#define CS42L84_CCM_SAMPLE_RATE     0x0601
#define CS42L84_CCM_RATE_48KHZ      0x04
#define CS42L84_CCM_CTL3            0x0602
#define CS42L84_CCM_REFCLK_DIVIDER  0x06
#define CS42L84_CCM_CTL4            0x0603
#define CS42L84_CCM_REFCLK_ENABLE   0x01
#define CS42L84_PLL_CTL1            0x0800
#define CS42L84_PLL_ENABLE          0x01
#define CS42L84_PLL_MODE            0x06
#define CS42L84_PLL_MODE_SCLK       0x06
#define CS42L84_PLL_DIV_FRAC0       0x0804
#define CS42L84_PLL_DIV_FRAC1       0x0805
#define CS42L84_PLL_DIV_FRAC2       0x0806
#define CS42L84_PLL_DIV_INT         0x0807
#define CS42L84_PLL_DIVOUT          0x0808
#define CS42L84_RING_SENSE_CTL      0x1282
#define CS42L84_TIP_SENSE_CTL       0x1283
#define CS42L84_SENSE_INVERT        0x80
#define CS42L84_RING_SENSE_UNKNOWN  0x40
#define CS42L84_SENSE_FALL          0x38
#define CS42L84_SENSE_RISE          0x07
#define CS42L84_DEBOUNCE_125MS      0x1
#define CS42L84_DEBOUNCE_500MS      0x3
#define CS42L84_TSRS_PLUG_STATUS    0x1288
#define CS42L84_TIP_SENSE_CTL2      0x1473
#define CS42L84_TIP_SENSE_SHORT_DET 0xc0
#define CS42L84_MISC_DET_CTL        0x1474
#define CS42L84_HSBIAS_CTL          0x06
#define CS42L84_HSBIAS_0V           0x02
#define CS42L84_HSBIAS_2V7          0x06
#define CS42L84_DETECT_MODE         0x18
#define CS42L84_PDN_MIC_LVL_DET     0x01
#define CS42L84_MIC_DET_CTL1        0x1475
#define CS42L84_HS_DET_LEVEL        0x3f
#define CS42L84_HS_DET_LEVEL_1V9    0x2c
#define CS42L84_MIC_DET_CTL4        0x1477
#define CS42L84_LATCH_TO_VP         0x02
#define CS42L84_HS_DET_STATUS2      0x147d
#define CS42L84_HS_LOAD             0x02
#define CS42L84_HS_DET_MASK         0x03
#define CS42L84_MSM_BLOCK_EN2       0x1801
#define CS42L84_BLOCK_ASP           0x40
#define CS42L84_BLOCK_BUS           0x20
#define CS42L84_BLOCK_DAC           0x10
#define CS42L84_BLOCK_ADC           0x08
#define CS42L84_MSM_BLOCK_EN3       0x1802
#define CS42L84_BLOCK_TR_SENSE      0x08
#define CS42L84_HS_DET_CTL2         0x1811
#define CS42L84_HS_DET_CTL          0xc0
#define CS42L84_HS_DET_SET          0x30
#define CS42L84_HS_DET_SET_SHIFT    4
#define CS42L84_HS_SWITCH_CTL       0x1812
#define CS42L84_HS_SWITCH_GROUNDED  0xf3
#define CS42L84_HS_SWITCH_HEADSET   0xa6
#define CS42L84_HS_CLAMP_DISABLE    0x1813
#define CS42L84_DAC_CTL1            0x3000
#define CS42L84_DAC_UNMUTE          0x01
#define CS42L84_DAC_CHA_VOL_LSB     0x3004
#define CS42L84_DAC_CHA_VOL_MSB     0x3005
#define CS42L84_DAC_CHB_VOL_LSB     0x3006
#define CS42L84_DAC_CHB_VOL_MSB     0x3007
#define CS42L84_HP_VOL_CTL          0x3020
#define CS42L84_HP_VOL_ZERO_CROSS   0x02
#define CS42L84_HP_VOL_SOFT         0x01
#define CS42L84_BUS_ASP_TX_SRC      0x4000
#define CS42L84_BUS_DAC_SRC         0x4001
#define CS42L84_SOURCE_ADC          0x07
#define CS42L84_SOURCE_ASP_RX_CH1   0x0d
#define CS42L84_SOURCE_ASP_RX_CH2   0x0e
#define CS42L84_ASP_CTL             0x5000
#define CS42L84_ASP_BCLK_ENABLE     0x02
#define CS42L84_ASP_TDM_MODE        0x04
#define CS42L84_ASP_FSYNC_CTL2      0x5010
#define CS42L84_ASP_FSYNC_CTL3      0x5011
#define CS42L84_ASP_RX_EN           0x5020
#define CS42L84_ASP_RX_CH1_CH2      0x03
#define CS42L84_ASP_TX_EN           0x5024
#define CS42L84_ASP_TX_CH1          0x01
#define CS42L84_ASP_RX_CH1_CTL1     0x5028
#define CS42L84_ASP_RX_CH1_CTL2     0x5029
#define CS42L84_ASP_RX_CH1_WIDTH    0x502a
#define CS42L84_ASP_RX_CH2_CTL1     0x502c
#define CS42L84_ASP_RX_CH2_CTL2     0x502d
#define CS42L84_ASP_RX_CH2_WIDTH    0x502e
#define CS42L84_ASP_TX_CH1_CTL1     0x5068
#define CS42L84_ASP_TX_CH1_CTL2     0x5069
#define CS42L84_ASP_TX_CH1_WIDTH    0x506a
#define CS42L84_ASP_TX_CH2_CTL1     0x506c
#define CS42L84_ASP_TX_CH2_CTL2     0x506d
#define CS42L84_ASP_TX_CH2_WIDTH    0x506e
#define CS42L84_SLOT_EDGE           0x01
#define CS42L84_SLOT_START_LSB      0xfe
#define CS42L84_SLOT_START_MSB      0x07
#define CS42L84_BOOT_US             3000
#define CS42L84_CLOCK_SWITCH_US     150
#define CS42L84_PLL_POLL_US         250
#define CS42L84_PLL_TIMEOUT_US      1250
#define CS42L84_DETECT_SETTLE_US    50000

typedef struct _APPLE_AUDIO_CODEC_STEP
{
    USHORT Register;
    UCHAR Mask;
    UCHAR Value;
} APPLE_AUDIO_CODEC_STEP;

typedef struct _APPLE_AUDIO_HW
{
    FDT_DRIVER_TREE Tree;
    ULONG McaNode;
    ULONG AdmacNode;
    ULONG CodecNode;
    APPLE_MCA Mca;
    APPLE_ADMAC Admac;
    APPLE_NCO Nco;
    APPLE_I2C I2c;
    APPLE_GPIO Reset;
    APPLE_ADMAC_CHANNEL Channel[APPLE_AUDIO_DIRECTIONS];
    ULONG Port;
    ULONG DmaChannel[APPLE_AUDIO_DIRECTIONS];
    ULONG SampleBits[APPLE_AUDIO_DIRECTIONS];
    UCHAR CodecAddress;
    UCHAR TipState;
    BOOLEAN Prepared[APPLE_AUDIO_DIRECTIONS];
    BOOLEAN Microphone;
    BOOLEAN Muted;
    LONG Volume[APPLE_AUDIO_CHANNELS];
} APPLE_AUDIO_HW;

static
VOID
AppleAudioSleep(
    _In_ ULONG Microseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10LL * Microseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
ULONG
AppleAudioFindLinkChild(
    _In_ PFDT_DRIVER_TREE Tree,
    _In_ ULONG Link,
    _In_z_ PCSTR Name)
{
    ULONG Child, Length;
    const CHAR *ChildName;

    for (Child = FdtFirstChild(&Tree->Fdt, Link); Child != FDT_NO_NODE;
         Child = FdtNextSibling(&Tree->Fdt, Child))
    {
        ChildName = FdtNodeName(&Tree->Fdt, Child, &Length);
        if (ChildName && FdtNameMatches(ChildName, Length, Name, (ULONG)strlen(Name)))
            return Child;
    }
    return FDT_NO_NODE;
}

static
NTSTATUS
AppleAudioFindNodes(
    _In_ PFDT_DRIVER_TREE Tree,
    _Out_ PULONG McaNode,
    _Out_ PULONG Port,
    _Out_ PULONG CodecNode,
    _Out_ PULONG AdmacNode,
    _Out_ PULONG DmaChannel,
    _Out_opt_ PULONG CaptureChannel)
{
    const CHAR *Name, *Names;
    const VOID *Dai, *Dmas;
    CHAR Wanted[8] = { 't', 'x', (CHAR)('0' + APPLE_AUDIO_FRONTEND), 'a', 0 };
    CHAR Capture[8] = { 'r', 'x', (CHAR)('0' + APPLE_AUDIO_FRONTEND), 'b', 0 };
    ULONG Link, Child, Length, NamesLength, Cursor = 0, EntryLength, Index, DmaCells;
    ULONG64 Value;

    for (Link = FdtFirstChild(&Tree->Fdt, Tree->Node); Link != FDT_NO_NODE;
         Link = FdtNextSibling(&Tree->Fdt, Link))
    {
        Name = FdtGetProperty(&Tree->Fdt, Link, "link-name", &Length);
        if (Name && Length == sizeof(APPLE_AUDIO_LINK_NAME) && !memcmp(Name, APPLE_AUDIO_LINK_NAME, Length))
            break;
    }
    if (Link == FDT_NO_NODE)
        return STATUS_NOT_FOUND;

    if ((Child = AppleAudioFindLinkChild(Tree, Link, "cpu")) == FDT_NO_NODE ||
        (Dai = FdtGetProperty(&Tree->Fdt, Child, "sound-dai", &Length)) == NULL ||
        Length != 2 * sizeof(ULONG) || !FdtReadCells(Dai, Length, 0, 1, &Value) ||
        (*McaNode = FdtDriverFindPhandle(Tree, (ULONG)Value)) == FDT_NO_NODE ||
        !FdtReadCells(Dai, Length, 1, 1, &Value))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    *Port = (ULONG)Value;
    if ((Child = AppleAudioFindLinkChild(Tree, Link, "codec")) == FDT_NO_NODE ||
        (Dai = FdtGetProperty(&Tree->Fdt, Child, "sound-dai", &Length)) == NULL ||
        Length != sizeof(ULONG) || !FdtReadCells(Dai, Length, 0, 1, &Value) ||
        (*CodecNode = FdtDriverFindPhandle(Tree, (ULONG)Value)) == FDT_NO_NODE)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Names = FdtGetProperty(&Tree->Fdt, *McaNode, "dma-names", &NamesLength);
    Dmas = FdtGetProperty(&Tree->Fdt, *McaNode, "dmas", &Length);
    for (Index = 0; Names && (Name = FdtNextString(Names, NamesLength, &Cursor, &EntryLength)) != NULL; ++Index)
    {
        if (EntryLength == strlen(Wanted) && !memcmp(Name, Wanted, EntryLength))
            break;
    }
    if (!Names || !Name || !Dmas || !FdtReadCells(Dmas, Length, 2 * Index, 1, &Value) ||
        (*AdmacNode = FdtDriverFindPhandle(Tree, (ULONG)Value)) == FDT_NO_NODE ||
        !FdtReadU32(&Tree->Fdt, *AdmacNode, "#dma-cells", &DmaCells) || DmaCells != 1 ||
        !FdtReadCells(Dmas, Length, 2 * Index + 1, 1, &Value))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    *DmaChannel = (ULONG)Value;
    if (!CaptureChannel)
        return STATUS_SUCCESS;
    *CaptureChannel = MAXULONG;
    Cursor = 0;
    for (Index = 0; (Name = FdtNextString(Names, NamesLength, &Cursor, &EntryLength)) != NULL; ++Index)
    {
        if (EntryLength == strlen(Capture) && !memcmp(Name, Capture, EntryLength))
            break;
    }
    if (Name && FdtReadCells(Dmas, Length, 2 * Index, 1, &Value) &&
        FdtDriverFindPhandle(Tree, (ULONG)Value) == *AdmacNode &&
        FdtReadCells(Dmas, Length, 2 * Index + 1, 1, &Value))
        *CaptureChannel = (ULONG)Value;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AppleAudioHwQueryInterrupt(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG Gsi,
    _Out_ PBOOLEAN EdgeTriggered)
{
    FDT_DRIVER_TREE Tree;
    ULONG McaNode, Port, CodecNode, AdmacNode, DmaChannel, Index;
    NTSTATUS Status;

    *Gsi = 0;
    *EdgeTriggered = FALSE;
    Status = FdtDriverOpen(PhysicalDeviceObject, &Tree);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = AppleAudioFindNodes(&Tree, &McaNode, &Port, &CodecNode, &AdmacNode, &DmaChannel, NULL);
    if (NT_SUCCESS(Status))
        Status = AppleAdmacQueryInterrupt(&Tree, AdmacNode, &Index, Gsi, EdgeTriggered);
    FdtDriverClose(&Tree);
    return Status;
}

static
NTSTATUS
CodecWrite(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ USHORT Register,
    _In_ UCHAR Value)
{
    UCHAR Buffer[3] = { (UCHAR)(Register >> 8), (UCHAR)Register, Value };

    return AppleI2cTransfer(&Hw->I2c, Hw->CodecAddress, Buffer, sizeof(Buffer), NULL, 0);
}

static
NTSTATUS
CodecRead(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ USHORT Register,
    _Out_ PUCHAR Value)
{
    UCHAR Buffer[2] = { (UCHAR)(Register >> 8), (UCHAR)Register };

    return AppleI2cTransfer(&Hw->I2c, Hw->CodecAddress, Buffer, sizeof(Buffer), Value, 1);
}

static
NTSTATUS
CodecUpdate(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ USHORT Register,
    _In_ UCHAR Mask,
    _In_ UCHAR Value)
{
    UCHAR Current;
    NTSTATUS Status;

    Status = CodecRead(Hw, Register, &Current);
    if (!NT_SUCCESS(Status))
        return Status;
    return CodecWrite(Hw, Register, (UCHAR)((Current & ~Mask) | (Value & Mask)));
}

static
NTSTATUS
CodecApplyVolume(
    _In_ PAPPLE_AUDIO_HW Hw)
{
    NTSTATUS Status;

    Status = CodecUpdate(Hw, CS42L84_FREEZE, CS42L84_FREEZE_ENGAGE, CS42L84_FREEZE_ENGAGE);
    if (NT_SUCCESS(Status))
        Status = CodecWrite(Hw, CS42L84_DAC_CHA_VOL_LSB, (UCHAR)Hw->Volume[0]);
    if (NT_SUCCESS(Status))
        Status = CodecWrite(Hw, CS42L84_DAC_CHA_VOL_MSB, (UCHAR)((Hw->Volume[0] >> 8) & 1));
    if (NT_SUCCESS(Status))
        Status = CodecWrite(Hw, CS42L84_DAC_CHB_VOL_LSB, (UCHAR)Hw->Volume[1]);
    if (NT_SUCCESS(Status))
        Status = CodecWrite(Hw, CS42L84_DAC_CHB_VOL_MSB, (UCHAR)((Hw->Volume[1] >> 8) & 1));
    CodecUpdate(Hw, CS42L84_FREEZE, CS42L84_FREEZE_ENGAGE, 0);
    return Status;
}

static
UCHAR
CodecTipState(
    _In_ UCHAR PlugStatus)
{
    return (UCHAR)((PlugStatus & (CS42L84_TS_PLUG | CS42L84_TS_UNPLUG)) >> 2);
}

static
BOOLEAN
CodecDetectHeadset(
    _In_ PAPPLE_AUDIO_HW Hw);

static
NTSTATUS
CodecInitialize(
    _In_ PAPPLE_AUDIO_HW Hw)
{
    static const struct
    {
        USHORT Register;
        UCHAR Mask;
        UCHAR Value;
    } Setup[] =
    {
        { CS42L84_MIC_DET_CTL4, CS42L84_LATCH_TO_VP, CS42L84_LATCH_TO_VP },
        { CS42L84_TIP_SENSE_CTL2, 0xc0, CS42L84_TIP_SENSE_SHORT_DET },
        { CS42L84_RING_SENSE_CTL, CS42L84_SENSE_INVERT | CS42L84_RING_SENSE_UNKNOWN | CS42L84_SENSE_FALL | CS42L84_SENSE_RISE,
          CS42L84_SENSE_INVERT | CS42L84_RING_SENSE_UNKNOWN | (CS42L84_DEBOUNCE_125MS << 3) | CS42L84_DEBOUNCE_125MS },
        { CS42L84_TIP_SENSE_CTL, CS42L84_SENSE_INVERT | CS42L84_SENSE_FALL | CS42L84_SENSE_RISE,
          CS42L84_SENSE_INVERT | (CS42L84_DEBOUNCE_125MS << 3) | CS42L84_DEBOUNCE_500MS },
        { CS42L84_MSM_BLOCK_EN3, CS42L84_BLOCK_TR_SENSE, CS42L84_BLOCK_TR_SENSE },
        { CS42L84_MIC_DET_CTL1, CS42L84_HS_DET_LEVEL, CS42L84_HS_DET_LEVEL_1V9 },
        { CS42L84_HS_SWITCH_CTL, 0xff, CS42L84_HS_SWITCH_GROUNDED },
        { CS42L84_HS_DET_CTL2, CS42L84_HS_DET_SET | CS42L84_HS_DET_CTL, 2 << CS42L84_HS_DET_SET_SHIFT },
        { CS42L84_HS_CLAMP_DISABLE, 0x01, 0x01 },
        { CS42L84_TSRS_PLUG_INT_MASK, CS42L84_RS_PLUG | CS42L84_RS_UNPLUG, CS42L84_RS_PLUG | CS42L84_RS_UNPLUG },
        { CS42L84_ASP_CTL, CS42L84_ASP_TDM_MODE, 0 },
        { CS42L84_HP_VOL_CTL, CS42L84_HP_VOL_SOFT | CS42L84_HP_VOL_ZERO_CROSS, CS42L84_HP_VOL_ZERO_CROSS },
        { CS42L84_ASP_RX_CH1_CTL1, CS42L84_SLOT_EDGE | CS42L84_SLOT_START_LSB, 0 },
        { CS42L84_ASP_RX_CH1_CTL2, CS42L84_SLOT_START_MSB, 0 },
        { CS42L84_ASP_RX_CH2_CTL1, CS42L84_SLOT_EDGE | CS42L84_SLOT_START_LSB, CS42L84_SLOT_EDGE },
        { CS42L84_ASP_RX_CH2_CTL2, CS42L84_SLOT_START_MSB, 0 },
        { CS42L84_ASP_TX_CH1_CTL1, CS42L84_SLOT_EDGE | CS42L84_SLOT_START_LSB, 0 },
        { CS42L84_ASP_TX_CH1_CTL2, CS42L84_SLOT_START_MSB, 0 },
        { CS42L84_ASP_TX_CH2_CTL1, CS42L84_SLOT_EDGE | CS42L84_SLOT_START_LSB, CS42L84_SLOT_EDGE },
        { CS42L84_ASP_TX_CH2_CTL2, CS42L84_SLOT_START_MSB, 0 },
        { CS42L84_BUS_DAC_SRC, 0xff, CS42L84_SOURCE_ASP_RX_CH1 | (CS42L84_SOURCE_ASP_RX_CH2 << 4) },
        { CS42L84_BUS_ASP_TX_SRC, 0xff, CS42L84_SOURCE_ADC },
    };
    UCHAR Id[3], Revision, Plug;
    ULONG Index, Chip;
    NTSTATUS Status;

    for (Index = 0; Index < RTL_NUMBER_OF(Id); ++Index)
    {
        Status = CodecRead(Hw, (USHORT)(CS42L84_DEVID + Index), &Id[Index]);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    Chip = ((ULONG)Id[0] << 12) | ((ULONG)Id[1] << 4) | (Id[2] >> 4);
    if (Chip != CS42L84_CHIP_ID)
    {
        DbgPrint("appleaudio: codec at %02x reports id %05lx, not a CS42L84\n", Hw->CodecAddress, Chip);
        return STATUS_NO_SUCH_DEVICE;
    }
    if (!NT_SUCCESS(CodecRead(Hw, 0x73fe, &Revision)))
        Revision = 0;
    for (Index = 0; Index < RTL_NUMBER_OF(Setup); ++Index)
    {
        Status = (Setup[Index].Mask == 0xff) ? CodecWrite(Hw, Setup[Index].Register, Setup[Index].Value) :
                 CodecUpdate(Hw, Setup[Index].Register, Setup[Index].Mask, Setup[Index].Value);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    Status = CodecRead(Hw, CS42L84_TSRS_PLUG_STATUS, &Plug);
    if (!NT_SUCCESS(Status))
        return Status;
    Hw->TipState = CodecTipState(Plug);
    Hw->Microphone = Hw->TipState == 3 && CodecDetectHeadset(Hw);
    Status = CodecApplyVolume(Hw);
    DbgPrint("appleaudio: CS42L84 revision %02x at %02x, %s %s\n", Revision, Hw->CodecAddress,
             Hw->Microphone ? "headset" : "headphones", Hw->TipState == 3 ? "plugged in" : "not plugged in");
    return Status;
}

NTSTATUS
NTAPI
AppleAudioHwOpen(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PAPPLE_AUDIO_HW *Hardware)
{
    PAPPLE_AUDIO_HW Hw;
    ULONG Dart, I2cNode, Address;
    const VOID *Iommus;
    ULONG Length;
    ULONG64 Value;
    NTSTATUS Status;

    *Hardware = NULL;
    Hw = ExAllocatePoolZero(NonPagedPool, sizeof(*Hw), TAG_APPLE_AUDIO_HW);
    if (!Hw)
        return STATUS_INSUFFICIENT_RESOURCES;
    Hw->Volume[0] = Hw->Volume[1] = APPLE_AUDIO_VOLUME_DEFAULT;

    Status = FdtDriverOpen(PhysicalDeviceObject, &Hw->Tree);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Hw, TAG_APPLE_AUDIO_HW);
        return Status;
    }
    Status = AppleAudioFindNodes(&Hw->Tree, &Hw->McaNode, &Hw->Port, &Hw->CodecNode, &Hw->AdmacNode,
                                 &Hw->DmaChannel[APPLE_AUDIO_RENDER], &Hw->DmaChannel[APPLE_AUDIO_CAPTURE]);
    if (!NT_SUCCESS(Status))
        goto Fail;

    Iommus = FdtGetProperty(&Hw->Tree.Fdt, Hw->AdmacNode, "iommus", &Length);
    if (Iommus && FdtReadCells(Iommus, Length, 0, 1, &Value) &&
        (Dart = FdtDriverFindPhandle(&Hw->Tree, (ULONG)Value)) != FDT_NO_NODE)
    {
        Status = ApplePmgrPowerOnDevice(&Hw->Tree, Dart);
        if (!NT_SUCCESS(Status))
            goto Fail;
    }
    Status = ApplePmgrPowerOnDevice(&Hw->Tree, Hw->McaNode);
    if (NT_SUCCESS(Status))
        Status = ApplePmgrPowerOnDevice(&Hw->Tree, Hw->AdmacNode);
    if (NT_SUCCESS(Status))
    {
        Status = ApplePmgrResetDevice(&Hw->Tree, Hw->McaNode);
        if (Status == STATUS_NOT_FOUND)
            Status = STATUS_SUCCESS;
    }
    if (NT_SUCCESS(Status))
        Status = AppleDartMapPhysical(&Hw->Tree, Hw->AdmacNode, "appleaudio");
    if (NT_SUCCESS(Status))
        Status = AppleMcaOpen(&Hw->Tree, Hw->McaNode, &Hw->Mca);
    if (NT_SUCCESS(Status))
        Status = AppleAdmacOpen(&Hw->Tree, Hw->AdmacNode, &Hw->Admac);
    if (NT_SUCCESS(Status))
        Status = AppleNcoOpen(&Hw->Tree, Hw->McaNode, APPLE_AUDIO_FRONTEND, &Hw->Nco);
    if (!NT_SUCCESS(Status))
        goto Fail;
    if (Hw->Port >= Hw->Mca.ClusterCount || Hw->DmaChannel[APPLE_AUDIO_RENDER] >= Hw->Admac.ChannelCount ||
        (Hw->DmaChannel[APPLE_AUDIO_RENDER] & 1))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Fail;
    }
    if (Hw->DmaChannel[APPLE_AUDIO_CAPTURE] >= Hw->Admac.ChannelCount || !(Hw->DmaChannel[APPLE_AUDIO_CAPTURE] & 1))
        Hw->DmaChannel[APPLE_AUDIO_CAPTURE] = MAXULONG;

    if ((I2cNode = FdtDriverFindParentNode(&Hw->Tree, Hw->CodecNode)) == FDT_NO_NODE ||
        !FdtReadU32(&Hw->Tree.Fdt, Hw->CodecNode, "reg", &Address) || Address > 0x7f)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Fail;
    }
    Hw->CodecAddress = (UCHAR)Address;
    Status = AppleI2cOpen(&Hw->Tree, I2cNode, &Hw->I2c);
    if (!NT_SUCCESS(Status))
        goto Fail;
    Status = AppleGpioOpen(&Hw->Tree, Hw->CodecNode, "reset-gpios", &Hw->Reset);
    if (NT_SUCCESS(Status))
    {
        AppleGpioSet(&Hw->Reset, FALSE);
        AppleAudioSleep(1000);
        AppleGpioSet(&Hw->Reset, TRUE);
    }
    else if (Status != STATUS_NOT_FOUND)
    {
        goto Fail;
    }
    AppleAudioSleep(CS42L84_BOOT_US);
    Status = CodecInitialize(Hw);
    if (!NT_SUCCESS(Status))
        goto Fail;

    DbgPrint("appleaudio: headphone jack on MCA port %lu from cluster %u, ADMAC channels %lu and %ld, interrupt %lu\n",
             Hw->Port, APPLE_AUDIO_FRONTEND, Hw->DmaChannel[APPLE_AUDIO_RENDER],
             (LONG)Hw->DmaChannel[APPLE_AUDIO_CAPTURE], Hw->Admac.InterruptGsi);
    *Hardware = Hw;
    return STATUS_SUCCESS;

Fail:
    DbgPrint("appleaudio: headphone jack bring-up failed 0x%08lx\n", Status);
    AppleAudioHwClose(Hw);
    return Status;
}

VOID
NTAPI
AppleAudioHwClose(
    _In_ PAPPLE_AUDIO_HW Hw)
{
    if (!Hw)
        return;
    AppleAudioHwUnprepare(Hw, APPLE_AUDIO_CAPTURE);
    AppleAudioHwUnprepare(Hw, APPLE_AUDIO_RENDER);
    if (Hw->Reset.Registers)
        AppleGpioSet(&Hw->Reset, FALSE);
    AppleGpioClose(&Hw->Reset);
    AppleI2cClose(&Hw->I2c);
    AppleNcoClose(&Hw->Nco);
    AppleAdmacClose(&Hw->Admac);
    AppleMcaClose(&Hw->Mca);
    FdtDriverClose(&Hw->Tree);
    ExFreePoolWithTag(Hw, TAG_APPLE_AUDIO_HW);
}

static
NTSTATUS
CodecRunSequence(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_reads_(Count) const APPLE_AUDIO_CODEC_STEP *Steps,
    _In_ ULONG Count)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    for (Index = 0; NT_SUCCESS(Status) && Index < Count; ++Index)
    {
        Status = (Steps[Index].Mask == 0xff) ? CodecWrite(Hw, Steps[Index].Register, Steps[Index].Value) :
                 CodecUpdate(Hw, Steps[Index].Register, Steps[Index].Mask, Steps[Index].Value);
    }
    return Status;
}

static
NTSTATUS
CodecStart(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ ULONG Direction,
    _In_ ULONG SampleBits,
    _In_ BOOLEAN Clocks)
{
    static const APPLE_AUDIO_CODEC_STEP ClockSetup[] =
    {
        { CS42L84_ASP_FSYNC_CTL2, 0xfe, (UCHAR)(((APPLE_AUDIO_SLOTS * APPLE_AUDIO_SLOT_WIDTH) & 0x7f) << 1) },
        { CS42L84_ASP_FSYNC_CTL3, 0x1f, (UCHAR)((APPLE_AUDIO_SLOTS * APPLE_AUDIO_SLOT_WIDTH) >> 7) },
        { CS42L84_PLL_CTL1, CS42L84_PLL_ENABLE, 0 },
        { CS42L84_CCM_CTL3, CS42L84_CCM_REFCLK_DIVIDER, 0 },
        { CS42L84_PLL_DIV_INT, 0xff, 0x40 },
        { CS42L84_PLL_DIV_FRAC0, 0xff, 0 },
        { CS42L84_PLL_DIV_FRAC1, 0xff, 0 },
        { CS42L84_PLL_DIV_FRAC2, 0xff, 0 },
        { CS42L84_PLL_CTL1, CS42L84_PLL_MODE, CS42L84_PLL_MODE_SCLK },
        { CS42L84_PLL_DIVOUT, 0xff, 0x10 },
        { CS42L84_CCM_SAMPLE_RATE, 0xff, CS42L84_CCM_RATE_48KHZ },
        { CS42L84_ASP_CTL, CS42L84_ASP_BCLK_ENABLE, CS42L84_ASP_BCLK_ENABLE },
        { CS42L84_MSM_BLOCK_EN2, CS42L84_BLOCK_ASP | CS42L84_BLOCK_BUS, CS42L84_BLOCK_ASP | CS42L84_BLOCK_BUS },
    };
    static const APPLE_AUDIO_CODEC_STEP Render[] =
    {
        { CS42L84_ASP_RX_EN, CS42L84_ASP_RX_CH1_CH2, CS42L84_ASP_RX_CH1_CH2 },
        { CS42L84_MSM_BLOCK_EN2, CS42L84_BLOCK_DAC, CS42L84_BLOCK_DAC },
    };
    static const APPLE_AUDIO_CODEC_STEP Capture[] =
    {
        { CS42L84_ASP_TX_EN, CS42L84_ASP_TX_CH1, CS42L84_ASP_TX_CH1 },
        { CS42L84_MSM_BLOCK_EN2, CS42L84_BLOCK_ADC, CS42L84_BLOCK_ADC },
    };
    static const APPLE_AUDIO_CODEC_STEP ClockStart[] =
    {
        { CS42L84_CCM_CTL4, CS42L84_CCM_REFCLK_ENABLE, CS42L84_CCM_REFCLK_ENABLE },
        { CS42L84_PLL_CTL1, CS42L84_PLL_ENABLE, CS42L84_PLL_ENABLE },
    };
    ULONG Waited;
    UCHAR Lock = 0;
    NTSTATUS Status;

    C_ASSERT(APPLE_AUDIO_BCLK == 3072000);
    if (Direction == APPLE_AUDIO_RENDER)
    {
        Status = CodecWrite(Hw, CS42L84_ASP_RX_CH1_WIDTH, (UCHAR)(SampleBits - 1));
        if (NT_SUCCESS(Status))
            Status = CodecWrite(Hw, CS42L84_ASP_RX_CH2_WIDTH, (UCHAR)(SampleBits - 1));
    }
    else
    {
        Status = CodecWrite(Hw, CS42L84_ASP_TX_CH1_WIDTH, (UCHAR)(SampleBits - 1));
        if (NT_SUCCESS(Status))
            Status = CodecWrite(Hw, CS42L84_ASP_TX_CH2_WIDTH, (UCHAR)(SampleBits - 1));
    }
    if (NT_SUCCESS(Status) && Clocks)
        Status = CodecRunSequence(Hw, ClockSetup, RTL_NUMBER_OF(ClockSetup));
    if (NT_SUCCESS(Status))
    {
        Status = (Direction == APPLE_AUDIO_RENDER) ? CodecRunSequence(Hw, Render, RTL_NUMBER_OF(Render)) :
                 CodecRunSequence(Hw, Capture, RTL_NUMBER_OF(Capture));
    }
    if (NT_SUCCESS(Status) && Clocks)
    {
        Status = CodecRunSequence(Hw, ClockStart, RTL_NUMBER_OF(ClockStart));
        for (Waited = 0; NT_SUCCESS(Status) && Waited <= CS42L84_PLL_TIMEOUT_US; Waited += CS42L84_PLL_POLL_US)
        {
            Status = CodecRead(Hw, CS42L84_PLL_LOCK_STATUS, &Lock);
            if (!NT_SUCCESS(Status) || (Lock & CS42L84_PLL_LOCKED))
                break;
            AppleAudioSleep(CS42L84_PLL_POLL_US);
        }
        if (NT_SUCCESS(Status) && !(Lock & CS42L84_PLL_LOCKED))
            DbgPrint("appleaudio: CS42L84 PLL did not lock on the 3.072 MHz bit clock (%02x)\n", Lock);
        if (NT_SUCCESS(Status))
            Status = CodecUpdate(Hw, CS42L84_CCM_CTL1, 0x0f, CS42L84_CCM_MCLK_SOURCE_PLL | CS42L84_CCM_MCLK_12_288MHZ);
        if (NT_SUCCESS(Status))
            AppleAudioSleep(CS42L84_CLOCK_SWITCH_US);
    }
    if (NT_SUCCESS(Status) && Direction == APPLE_AUDIO_RENDER)
        Status = CodecUpdate(Hw, CS42L84_DAC_CTL1, CS42L84_DAC_UNMUTE, Hw->Muted ? 0 : CS42L84_DAC_UNMUTE);
    return Status;
}

static
VOID
CodecStop(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ ULONG Direction,
    _In_ BOOLEAN Clocks)
{
    if (Direction == APPLE_AUDIO_RENDER)
        CodecUpdate(Hw, CS42L84_DAC_CTL1, CS42L84_DAC_UNMUTE, 0);
    if (Clocks)
    {
        CodecWrite(Hw, CS42L84_CCM_CTL1, 0);
        AppleAudioSleep(CS42L84_CLOCK_SWITCH_US);
        CodecUpdate(Hw, CS42L84_PLL_CTL1, CS42L84_PLL_ENABLE, 0);
        CodecUpdate(Hw, CS42L84_CCM_CTL4, CS42L84_CCM_REFCLK_ENABLE, 0);
    }
    if (Direction == APPLE_AUDIO_RENDER)
    {
        CodecUpdate(Hw, CS42L84_MSM_BLOCK_EN2, CS42L84_BLOCK_DAC, 0);
        CodecUpdate(Hw, CS42L84_ASP_RX_EN, CS42L84_ASP_RX_CH1_CH2, 0);
    }
    else
    {
        CodecUpdate(Hw, CS42L84_ASP_TX_EN, CS42L84_ASP_TX_CH1, 0);
        CodecUpdate(Hw, CS42L84_MSM_BLOCK_EN2, CS42L84_BLOCK_ADC, 0);
    }
    if (Clocks)
    {
        CodecUpdate(Hw, CS42L84_MSM_BLOCK_EN2, CS42L84_BLOCK_ASP | CS42L84_BLOCK_BUS, 0);
        CodecUpdate(Hw, CS42L84_ASP_CTL, CS42L84_ASP_BCLK_ENABLE, 0);
    }
}

BOOLEAN
NTAPI
AppleAudioHwHasCapture(
    _In_ PAPPLE_AUDIO_HW Hw)
{
    return Hw->DmaChannel[APPLE_AUDIO_CAPTURE] != MAXULONG;
}

NTSTATUS
NTAPI
AppleAudioHwPrepare(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ ULONG Direction,
    _In_ ULONG SampleBits)
{
    ULONG Channels = (Direction == APPLE_AUDIO_RENDER) ? APPLE_AUDIO_CHANNELS : APPLE_AUDIO_CAPTURE_CHANNELS;
    BOOLEAN Clocks;
    NTSTATUS Status;

    if (Direction >= APPLE_AUDIO_DIRECTIONS)
        return STATUS_INVALID_PARAMETER;
    if (Hw->Prepared[Direction])
        return SampleBits == Hw->SampleBits[Direction] ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
    if (SampleBits != 16 && SampleBits != 32)
        return STATUS_NOT_SUPPORTED;
    if (Hw->DmaChannel[Direction] == MAXULONG)
        return STATUS_NOT_SUPPORTED;
    Clocks = !Hw->Prepared[Direction ^ 1];
    if (Direction == APPLE_AUDIO_RENDER)
        Status = AppleMcaConfigureTx(&Hw->Mca, APPLE_AUDIO_FRONTEND, APPLE_AUDIO_SLOTS, APPLE_AUDIO_SLOT_WIDTH,
                                     Channels, SampleBits);
    else
        Status = AppleMcaConfigureRx(&Hw->Mca, APPLE_AUDIO_FRONTEND, APPLE_AUDIO_SLOTS, APPLE_AUDIO_SLOT_WIDTH,
                                     Channels, SampleBits, 1UL << Hw->Port);
    if (NT_SUCCESS(Status) && Clocks)
        Status = AppleNcoSetRate(&Hw->Nco, APPLE_AUDIO_BCLK);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Clocks)
    {
        AppleNcoEnable(&Hw->Nco, TRUE);
        Status = AppleMcaRoutePort(&Hw->Mca, Hw->Port, APPLE_AUDIO_FRONTEND, TRUE);
        if (!NT_SUCCESS(Status))
        {
            AppleNcoEnable(&Hw->Nco, FALSE);
            return Status;
        }
        AppleMcaEnableClocks(&Hw->Mca, APPLE_AUDIO_FRONTEND, TRUE);
    }
    Status = CodecStart(Hw, Direction, SampleBits, Clocks);
    if (NT_SUCCESS(Status))
        Status = AppleAdmacOpenChannel(&Hw->Admac, Hw->DmaChannel[Direction], SampleBits / 8, Channels,
                                       &Hw->Channel[Direction]);
    Hw->SampleBits[Direction] = SampleBits;
    Hw->Prepared[Direction] = TRUE;
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaudio: %s preparation failed 0x%08lx\n", Direction == APPLE_AUDIO_RENDER ? "playback" : "capture",
                 Status);
        AppleAudioHwUnprepare(Hw, Direction);
    }
    return Status;
}

VOID
NTAPI
AppleAudioHwUnprepare(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ ULONG Direction)
{
    BOOLEAN Clocks;

    if (Direction >= APPLE_AUDIO_DIRECTIONS || !Hw->Prepared[Direction])
        return;
    AppleAdmacCloseChannel(&Hw->Channel[Direction]);
    Clocks = !Hw->Prepared[Direction ^ 1];
    CodecStop(Hw, Direction, Clocks);
    if (Clocks)
    {
        AppleMcaEnableClocks(&Hw->Mca, APPLE_AUDIO_FRONTEND, FALSE);
        AppleMcaRoutePort(&Hw->Mca, Hw->Port, APPLE_AUDIO_FRONTEND, FALSE);
        AppleNcoEnable(&Hw->Nco, FALSE);
    }
    Hw->Prepared[Direction] = FALSE;
}

NTSTATUS
NTAPI
AppleAudioHwArm(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ ULONG Direction)
{
    if (Direction >= APPLE_AUDIO_DIRECTIONS || !Hw->Prepared[Direction])
        return STATUS_INVALID_DEVICE_STATE;
    return (Direction == APPLE_AUDIO_RENDER) ? AppleMcaResetTx(&Hw->Mca, APPLE_AUDIO_FRONTEND) :
           AppleMcaResetRx(&Hw->Mca, APPLE_AUDIO_FRONTEND);
}

NTSTATUS
NTAPI
AppleAudioHwStartDma(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ ULONG Direction,
    _In_ ULONG64 Buffer,
    _In_ ULONG BufferLength,
    _In_ ULONG PeriodLength)
{
    NTSTATUS Status;

    if (Direction >= APPLE_AUDIO_DIRECTIONS || !Hw->Prepared[Direction] || !Hw->Channel[Direction].Admac)
        return STATUS_INVALID_DEVICE_STATE;
    Status = AppleAdmacStartCyclic(&Hw->Channel[Direction], Buffer, BufferLength, PeriodLength);
    if (NT_SUCCESS(Status))
    {
        if (Direction == APPLE_AUDIO_RENDER)
            AppleMcaEnableTx(&Hw->Mca, APPLE_AUDIO_FRONTEND, TRUE);
        else
            AppleMcaEnableRx(&Hw->Mca, APPLE_AUDIO_FRONTEND, TRUE);
    }
    return Status;
}

VOID
NTAPI
AppleAudioHwStopDma(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ ULONG Direction)
{
    if (Direction >= APPLE_AUDIO_DIRECTIONS)
        return;
    if (Direction == APPLE_AUDIO_RENDER)
        AppleMcaEnableTx(&Hw->Mca, APPLE_AUDIO_FRONTEND, FALSE);
    else
        AppleMcaEnableRx(&Hw->Mca, APPLE_AUDIO_FRONTEND, FALSE);
    if (Hw->Channel[Direction].Admac)
        AppleAdmacStop(&Hw->Channel[Direction]);
}

BOOLEAN
NTAPI
AppleAudioHwInterrupt(
    _In_ PAPPLE_AUDIO_HW Hw,
    _Out_writes_(APPLE_AUDIO_DIRECTIONS) PULONG Periods)
{
    BOOLEAN Global, AnyGlobal = FALSE, Claimed = FALSE, Mine;
    ULONG Direction;

    for (Direction = 0; Direction < APPLE_AUDIO_DIRECTIONS; ++Direction)
    {
        Periods[Direction] = 0;
        if (Hw->DmaChannel[Direction] == MAXULONG)
            continue;
        Mine = AppleAdmacInterruptPending(&Hw->Admac, Hw->DmaChannel[Direction], &Global);
        AnyGlobal |= Global;
        if (Mine && Hw->Channel[Direction].Admac)
            Periods[Direction] = AppleAdmacService(&Hw->Channel[Direction]);
        Claimed |= Mine;
    }
    return Claimed || AnyGlobal;
}

ULONG
NTAPI
AppleAudioHwPosition(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ ULONG Direction)
{
    if (Direction >= APPLE_AUDIO_DIRECTIONS || !Hw->Channel[Direction].Admac)
        return 0;
    return AppleAdmacPosition(&Hw->Channel[Direction]);
}

NTSTATUS
NTAPI
AppleAudioHwSetVolume(
    _In_ PAPPLE_AUDIO_HW Hw,
    _In_ LONG LeftHalfDecibels,
    _In_ LONG RightHalfDecibels,
    _In_ BOOLEAN Mute)
{
    NTSTATUS Status;

    Hw->Volume[0] = max(min(LeftHalfDecibels, APPLE_AUDIO_VOLUME_MAXIMUM), APPLE_AUDIO_VOLUME_MINIMUM);
    Hw->Volume[1] = max(min(RightHalfDecibels, APPLE_AUDIO_VOLUME_MAXIMUM), APPLE_AUDIO_VOLUME_MINIMUM);
    Hw->Muted = Mute;
    Status = CodecApplyVolume(Hw);
    if (NT_SUCCESS(Status) && Hw->Prepared[APPLE_AUDIO_RENDER])
        Status = CodecUpdate(Hw, CS42L84_DAC_CTL1, CS42L84_DAC_UNMUTE, Mute ? 0 : CS42L84_DAC_UNMUTE);
    return Status;
}

static
BOOLEAN
CodecDetectHeadset(
    _In_ PAPPLE_AUDIO_HW Hw)
{
    UCHAR Detect = 0;

    CodecUpdate(Hw, CS42L84_MISC_DET_CTL, CS42L84_HSBIAS_CTL | CS42L84_DETECT_MODE, CS42L84_HSBIAS_2V7);
    CodecUpdate(Hw, CS42L84_MISC_DET_CTL, CS42L84_PDN_MIC_LVL_DET, 0);
    AppleAudioSleep(CS42L84_DETECT_SETTLE_US);
    CodecWrite(Hw, CS42L84_HS_SWITCH_CTL, CS42L84_HS_SWITCH_HEADSET);
    CodecUpdate(Hw, CS42L84_HS_DET_CTL2, CS42L84_HS_DET_SET, 0);
    CodecUpdate(Hw, CS42L84_MISC_DET_CTL, CS42L84_DETECT_MODE, CS42L84_DETECT_MODE);
    AppleAudioSleep(CS42L84_DETECT_SETTLE_US);
    if (!NT_SUCCESS(CodecRead(Hw, CS42L84_HS_DET_STATUS2, &Detect)))
        Detect = 0;
    CodecUpdate(Hw, CS42L84_MISC_DET_CTL, CS42L84_PDN_MIC_LVL_DET, CS42L84_PDN_MIC_LVL_DET);
    if ((Detect & CS42L84_HS_DET_MASK) != CS42L84_HS_LOAD)
        CodecUpdate(Hw, CS42L84_MISC_DET_CTL, CS42L84_HSBIAS_CTL, CS42L84_HSBIAS_0V);
    return (Detect & CS42L84_HS_DET_MASK) == CS42L84_HS_LOAD;
}

static
VOID
CodecRevertHeadset(
    _In_ PAPPLE_AUDIO_HW Hw)
{
    CodecUpdate(Hw, CS42L84_MISC_DET_CTL, CS42L84_HSBIAS_CTL | CS42L84_DETECT_MODE, CS42L84_HSBIAS_0V);
    CodecWrite(Hw, CS42L84_HS_SWITCH_CTL, CS42L84_HS_SWITCH_GROUNDED);
    CodecUpdate(Hw, CS42L84_HS_DET_CTL2, CS42L84_HS_DET_SET, 2 << CS42L84_HS_DET_SET_SHIFT);
}

BOOLEAN
NTAPI
AppleAudioHwPollJack(
    _In_ PAPPLE_AUDIO_HW Hw,
    _Out_ PBOOLEAN Connected,
    _Out_ PBOOLEAN Microphone)
{
    UCHAR Plug, Tip;

    *Connected = Hw->TipState == 3;
    *Microphone = Hw->Microphone;
    if (!NT_SUCCESS(CodecRead(Hw, CS42L84_TSRS_PLUG_STATUS, &Plug)))
        return FALSE;
    Tip = CodecTipState(Plug);
    if (Tip != 0 && Tip != 3)
        return FALSE;
    if (Tip == Hw->TipState)
        return FALSE;
    Hw->TipState = Tip;
    *Connected = Tip == 3;
    if (*Connected)
    {
        Hw->Microphone = CodecDetectHeadset(Hw);
    }
    else
    {
        CodecRevertHeadset(Hw);
        Hw->Microphone = FALSE;
    }
    *Microphone = Hw->Microphone;
    DbgPrint("appleaudio: %s %s\n", *Microphone ? "headset" : "headphones", *Connected ? "plugged in" : "unplugged");
    return TRUE;
}
