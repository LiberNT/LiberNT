/*
 * PROJECT:     LiberNT Apple Silicon Audio Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     TI TAS2764 / SN012776 speaker amplifier control
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "tas2764.h"

#define TAS2764_PAGE_SELECT         0x00
#define TAS2764_SW_RESET            0x01
#define TAS2764_SW_RESET_ASSERT     0x01
#define TAS2764_MODE_CTRL           0x02
#define TAS2764_MODE_MASK           0x07
#define TAS2764_MODE_ACTIVE         0x00
#define TAS2764_MODE_MUTE           0x01
#define TAS2764_MODE_SHUTDOWN       0x02
#define TAS2764_MODE_VSNS_PD        0x08
#define TAS2764_MODE_ISNS_PD        0x10
#define TAS2764_MODE_BOP_SRC        0x80
#define TAS2764_CHNL_0              0x03
#define TAS2764_AMP_LEVEL_MASK      0x3e
#define TAS2764_AMP_LEVEL_SHIFT     1
#define TAS2764_AMP_LEVEL_MAXIMUM   0x14
#define TAS2764_AMP_LEVEL_BASE_MDB  11000
#define TAS2764_AMP_LEVEL_STEP_MDB  500
#define TAS2764_DC_BLK0             0x04
#define TAS2764_HPF_MASK            0x07
#define TAS2764_HPF_2HZ             0x01
#define TAS2764_HPF_800HZ           0x06
#define TAS2764_MISC_CFG1           0x06
#define TAS2764_OCE_RETRY           0x20
#define TAS2764_TDM_CFG0            0x08
#define TAS2764_TDM_CFG0_MASK       0x2f
#define TAS2764_TDM_CFG0_48KHZ      0x08
#define TAS2764_TDM_CFG1            0x09
#define TAS2764_TDM_CFG1_MASK       0x3f
#define TAS2764_RX_OFFSET_I2S       0x02
#define TAS2764_RX_EDGE_FALLING     0x01
#define TAS2764_TDM_CFG2            0x0a
#define TAS2764_RX_SCFG_MASK        0x30
#define TAS2764_RX_SCFG_LEFT        0x10
#define TAS2764_RX_WLEN_MASK        0x0c
#define TAS2764_RX_WLEN_16          0x00
#define TAS2764_RX_WLEN_32          0x0c
#define TAS2764_RX_SLEN_MASK        0x03
#define TAS2764_RX_SLEN_32          0x02
#define TAS2764_TDM_CFG3            0x0c
#define TAS2764_RX_SLOT_R_SHIFT     4
#define TAS2764_RX_SLOT_MASK        0x0f
#define TAS2764_TDM_CFG4            0x0d
#define TAS2764_TX_EDGE_FALLING     0x01
#define TAS2764_TDM_CFG5            0x0e
#define TAS2764_TDM_CFG6            0x0f
#define TAS2764_SENSE_TX            0x40
#define TAS2764_SENSE_SLOT_MASK     0x3f
#define TAS2764_DVC                 0x1a
#define TAS2764_BOP_CFG0            0x1d
#define TAS2764_NG_CFG0             0x35
#define TAS2764_INT_MASK0           0x3b
#define TAS2764_INT_MASK1           0x3c
#define TAS2764_INT_MASK4           0x3d
#define TAS2764_INT_MASK2           0x40
#define TAS2764_INT_MASK3           0x41
#define TAS2764_INT_LTCH0           0x49
#define TAS2764_INT_LTCH1           0x4a
#define TAS2764_INT_LTCH1_0         0x4b
#define TAS2764_INT_LTCH2           0x4f
#define TAS2764_INT_LTCH3           0x50
#define TAS2764_INT_LTCH4           0x51
#define TAS2764_TEMP                0x56
#define TAS2764_TEMP_OFFSET         93
#define TAS2764_INT_CLK_CFG         0x5c
#define TAS2764_IRQZ_CLR            0x04
#define TAS2764_NG_CFG2             0x6b
#define TAS2764_DAC_MOD_RST         0x76
#define TAS2764_REV_ID              0x7d
#define TAS2764_BOOK                0x7f
#define TAS2764_SDOUT_HIZ_1         0x3d
#define TAS2764_SDOUT_HIZ_BYTES     4
#define TAS2764_SDOUT_HIZ_9         0x45
#define TAS2764_SDOUT_FORCE_ZERO    0x80
#define TAS2764_PAGE_UNKNOWN        0xff

typedef struct _TAS2764_WRITE
{
    UCHAR Page;
    UCHAR Register;
    UCHAR Value;
} TAS2764_WRITE;

static const UCHAR Sn012776BopPresets[] =
{
    0x01, 0x32, 0x02, 0x22, 0x83, 0x2d, 0x80, 0x02, 0x06, 0x32, 0x46, 0x30,
    0x02, 0x06, 0x38, 0x40, 0x30, 0x02, 0x06, 0x3e, 0x37, 0x30, 0xff, 0xe6
};

static const TAS2764_WRITE Sn012776Setup[] =
{
    { 0x00, TAS2764_NG_CFG0, 0xb0 },
    { 0x00, TAS2764_DAC_MOD_RST, 0x00 },
    { 0x00, TAS2764_NG_CFG2, 0x41 },
    { 0x01, 0x33, 0x80 },
    { 0x01, 0x37, 0x3a },
    { 0x06, 0x14, 0x00 },
    { 0x06, 0x15, 0x13 },
    { 0x06, 0x16, 0x52 },
    { 0x06, 0x17, 0x00 },
    { 0x06, 0x18, 0xe4 },
    { 0x06, 0x19, 0x0c },
    { 0x06, 0x1a, 0xaa },
    { 0x06, 0x1b, 0x00 },
    { 0x06, 0x1c, 0x12 },
    { 0x06, 0x1d, 0xa0 },
    { 0x06, 0x1e, 0xd8 },
    { 0x06, 0x1f, 0x00 },
    { 0xfd, 0x0d, 0x0d },
    { 0xfd, 0x6c, 0x02 },
    { 0xfd, 0x6d, 0x0f },
    { 0xfd, 0x0d, 0x00 },
};

static const PCSTR Tas2764FaultNames[8] =
{
    "over temperature",
    "over current",
    "bad TDM clock",
    "limiter active",
    "PVDD below limiter inflection point",
    "limiter maximum attenuation",
    "BOP infinite hold",
    "BOP mute"
};

static
NTSTATUS
Tas2764SelectPage(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ UCHAR Page)
{
    UCHAR Buffer[2] = { TAS2764_PAGE_SELECT, Page };
    NTSTATUS Status;

    if (Amp->Page == Page)
        return STATUS_SUCCESS;
    Status = AppleI2cTransfer(Amp->I2c, Amp->Address, Buffer, sizeof(Buffer), NULL, 0);
    Amp->Page = NT_SUCCESS(Status) ? Page : TAS2764_PAGE_UNKNOWN;
    return Status;
}

static
NTSTATUS
Tas2764Write(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ UCHAR Page,
    _In_ UCHAR Register,
    _In_ UCHAR Value)
{
    UCHAR Buffer[2] = { Register, Value };
    NTSTATUS Status;

    ASSERT(Register != TAS2764_PAGE_SELECT);
    Status = Tas2764SelectPage(Amp, Page);
    if (NT_SUCCESS(Status))
        Status = AppleI2cTransfer(Amp->I2c, Amp->Address, Buffer, sizeof(Buffer), NULL, 0);
    return Status;
}

static
NTSTATUS
Tas2764Read(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ UCHAR Page,
    _In_ UCHAR Register,
    _Out_ PUCHAR Value)
{
    NTSTATUS Status;

    *Value = 0;
    Status = Tas2764SelectPage(Amp, Page);
    if (NT_SUCCESS(Status))
        Status = AppleI2cTransfer(Amp->I2c, Amp->Address, &Register, 1, Value, 1);
    return Status;
}

static
NTSTATUS
Tas2764Update(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ UCHAR Page,
    _In_ UCHAR Register,
    _In_ UCHAR Mask,
    _In_ UCHAR Value)
{
    UCHAR Current;
    NTSTATUS Status;

    Status = Tas2764Read(Amp, Page, Register, &Current);
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, Page, Register, (UCHAR)((Current & ~Mask) | (Value & Mask)));
    return Status;
}

LONG
Tas2764AmpGainMilliDb(
    _In_ ULONG Level)
{
    ASSERT(Level <= TAS2764_AMP_LEVEL_MAXIMUM);
    return TAS2764_AMP_LEVEL_BASE_MDB + (LONG)Level * TAS2764_AMP_LEVEL_STEP_MDB;
}

NTSTATUS
Tas2764Reset(
    _Inout_ PAPPLE_AUDIO_AMP Amp)
{
    NTSTATUS Status;

    Amp->Page = TAS2764_PAGE_UNKNOWN;
    Amp->Volume = TAS2764_VOLUME_UNKNOWN;
    Status = Tas2764SelectPage(Amp, 0);
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, 0, TAS2764_BOOK, 0);
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, 0, TAS2764_SW_RESET, TAS2764_SW_RESET_ASSERT);
    Amp->Page = NT_SUCCESS(Status) ? 0 : TAS2764_PAGE_UNKNOWN;
    return Status;
}

NTSTATUS
Tas2764Initialize(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ ULONG AmpLevel)
{
    UCHAR Mode, Revision, Mask[TAS2764_SDOUT_HIZ_BYTES];
    ULONG Index;
    NTSTATUS Status;

    if (AmpLevel > TAS2764_AMP_LEVEL_MAXIMUM || Amp->ImonSlot > TAS2764_SENSE_SLOT_MASK ||
        Amp->VmonSlot > TAS2764_SENSE_SLOT_MASK || Amp->Side > TAS2764_RX_SLOT_MASK)
        return STATUS_INVALID_PARAMETER;
    Status = Tas2764Read(Amp, 0, TAS2764_MODE_CTRL, &Mode);
    if (NT_SUCCESS(Status))
        Status = Tas2764Read(Amp, 0, TAS2764_REV_ID, &Revision);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("appleaudio: amp %s at %02x does not answer (0x%08lx)\n", Amp->Name, Amp->Address, Status);
        return Status;
    }
    DbgPrint("appleaudio: amp %s at %02x: revision %u, PG %u, mode %02x after reset\n", Amp->Name, Amp->Address,
             Revision >> 4, Revision & 0x0f, Mode);
    if ((Mode & TAS2764_MODE_MASK) != TAS2764_MODE_SHUTDOWN)
        return STATUS_NO_SUCH_DEVICE;

    Status = Tas2764Write(Amp, 0, TAS2764_INT_MASK0, 0x00);
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, 0, TAS2764_INT_MASK1, 0xff);
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, 0, TAS2764_INT_MASK2, 0xff);
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, 0, TAS2764_INT_MASK3, 0xff);
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, 0, TAS2764_INT_MASK4, 0xff);
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, 0, TAS2764_TDM_CFG5, (UCHAR)(TAS2764_SENSE_TX | Amp->VmonSlot));
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, 0, TAS2764_TDM_CFG6, (UCHAR)(TAS2764_SENSE_TX | Amp->ImonSlot));
    if (NT_SUCCESS(Status) && Amp->Sn012776)
    {
        Status = Tas2764Update(Amp, 0, TAS2764_MODE_CTRL, TAS2764_MODE_BOP_SRC, TAS2764_MODE_BOP_SRC);
        for (Index = 0; NT_SUCCESS(Status) && Index < RTL_NUMBER_OF(Sn012776BopPresets); ++Index)
            Status = Tas2764Write(Amp, 0, (UCHAR)(TAS2764_BOP_CFG0 + Index), Sn012776BopPresets[Index]);
        for (Index = 0; NT_SUCCESS(Status) && Index < RTL_NUMBER_OF(Sn012776Setup); ++Index)
            Status = Tas2764Write(Amp, Sn012776Setup[Index].Page, Sn012776Setup[Index].Register,
                                  Sn012776Setup[Index].Value);
    }
    if (NT_SUCCESS(Status))
        Status = Tas2764Update(Amp, 0, TAS2764_TDM_CFG0, TAS2764_TDM_CFG0_MASK, TAS2764_TDM_CFG0_48KHZ);
    if (NT_SUCCESS(Status))
        Status = Tas2764Update(Amp, 0, TAS2764_TDM_CFG1, TAS2764_TDM_CFG1_MASK,
                               TAS2764_RX_OFFSET_I2S | TAS2764_RX_EDGE_FALLING);
    if (NT_SUCCESS(Status))
        Status = Tas2764Update(Amp, 0, TAS2764_TDM_CFG2, TAS2764_RX_SCFG_MASK | TAS2764_RX_SLEN_MASK,
                               TAS2764_RX_SCFG_LEFT | TAS2764_RX_SLEN_32);
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, 0, TAS2764_TDM_CFG3, (UCHAR)((Amp->Side << TAS2764_RX_SLOT_R_SHIFT) | Amp->Side));
    if (NT_SUCCESS(Status))
        Status = Tas2764Update(Amp, 0, TAS2764_TDM_CFG4, TAS2764_TX_EDGE_FALLING, 0);
    if (NT_SUCCESS(Status) && Amp->IdleZero)
    {
        for (Index = 0; Index < TAS2764_SDOUT_HIZ_BYTES; ++Index)
            Mask[Index] = (UCHAR)(Amp->IdleMask >> (Index * 8));
        for (Index = 0; NT_SUCCESS(Status) && Index < TAS2764_SDOUT_HIZ_BYTES; ++Index)
            Status = Tas2764Write(Amp, 1, (UCHAR)(TAS2764_SDOUT_HIZ_1 + Index), Mask[Index]);
        if (NT_SUCCESS(Status))
            Status = Tas2764Update(Amp, 1, TAS2764_SDOUT_HIZ_9, TAS2764_SDOUT_FORCE_ZERO, TAS2764_SDOUT_FORCE_ZERO);
    }
    if (NT_SUCCESS(Status))
        Status = Tas2764Update(Amp, 0, TAS2764_CHNL_0, TAS2764_AMP_LEVEL_MASK, (UCHAR)(AmpLevel << TAS2764_AMP_LEVEL_SHIFT));
    if (NT_SUCCESS(Status))
        Status = Tas2764Update(Amp, 0, TAS2764_DC_BLK0, TAS2764_HPF_MASK, Amp->Tweeter ? TAS2764_HPF_800HZ : TAS2764_HPF_2HZ);
    if (NT_SUCCESS(Status))
        Status = Tas2764Update(Amp, 0, TAS2764_MISC_CFG1, TAS2764_OCE_RETRY, TAS2764_OCE_RETRY);
    if (NT_SUCCESS(Status))
        Status = Tas2764Write(Amp, 0, TAS2764_DVC, TAS2764_DVC_MUTE);
    if (NT_SUCCESS(Status))
        Amp->Volume = TAS2764_DVC_MUTE;
    if (NT_SUCCESS(Status))
        Status = Tas2764Update(Amp, 0, TAS2764_INT_CLK_CFG, TAS2764_IRQZ_CLR, TAS2764_IRQZ_CLR);
    if (NT_SUCCESS(Status))
        Status = Tas2764Read(Amp, 0, TAS2764_MODE_CTRL, &Mode);
    if (NT_SUCCESS(Status))
        DbgPrint("appleaudio: amp %s at %02x: %s, RX slot %lu, I/V sense slots %u/%u, gain %ld mdBV, HPF %s, mode %02x, muted\n",
                 Amp->Name, Amp->Address, Amp->Sn012776 ? "SN012776" : "TAS2764", Amp->Side, Amp->ImonSlot,
                 Amp->VmonSlot, Tas2764AmpGainMilliDb(AmpLevel), Amp->Tweeter ? "800 Hz" : "2 Hz", Mode);
    else
        DbgPrint("appleaudio: amp %s at %02x: setup failed 0x%08lx\n", Amp->Name, Amp->Address, Status);
    return Status;
}

NTSTATUS
Tas2764SetFormat(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ ULONG SampleBits)
{
    if (SampleBits != 16 && SampleBits != 32)
        return STATUS_NOT_SUPPORTED;
    return Tas2764Update(Amp, 0, TAS2764_TDM_CFG2, TAS2764_RX_WLEN_MASK,
                         SampleBits == 16 ? TAS2764_RX_WLEN_16 : TAS2764_RX_WLEN_32);
}

NTSTATUS
Tas2764SetMode(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ TAS2764_MODE Mode)
{
    UCHAR Value;

    switch (Mode)
    {
        case Tas2764Active:
            Value = TAS2764_MODE_ACTIVE;
            break;
        case Tas2764Muted:
            Value = TAS2764_MODE_MUTE;
            break;
        case Tas2764Shutdown:
            Value = TAS2764_MODE_SHUTDOWN | TAS2764_MODE_VSNS_PD | TAS2764_MODE_ISNS_PD;
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }
    return Tas2764Update(Amp, 0, TAS2764_MODE_CTRL, TAS2764_MODE_MASK | TAS2764_MODE_VSNS_PD | TAS2764_MODE_ISNS_PD,
                         Value);
}

NTSTATUS
Tas2764SetVolume(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _In_ UCHAR Volume)
{
    NTSTATUS Status;

    if (Volume == Amp->Volume)
        return STATUS_SUCCESS;
    Status = Tas2764Write(Amp, 0, TAS2764_DVC, Volume);
    Amp->Volume = NT_SUCCESS(Status) ? Volume : TAS2764_VOLUME_UNKNOWN;
    return Status;
}

UCHAR
Tas2764PollFaults(
    _Inout_ PAPPLE_AUDIO_AMP Amp)
{
    static const UCHAR Context[] = { TAS2764_INT_LTCH1, TAS2764_INT_LTCH1_0, TAS2764_INT_LTCH2, TAS2764_INT_LTCH3,
                                     TAS2764_INT_LTCH4 };
    UCHAR Latched, Other[RTL_NUMBER_OF(Context)];
    ULONG Index;

    if (!NT_SUCCESS(Tas2764Read(Amp, 0, TAS2764_INT_LTCH0, &Latched)) || !Latched)
        return 0;
    for (Index = 0; Index < RTL_NUMBER_OF(Context); ++Index)
    {
        if (!NT_SUCCESS(Tas2764Read(Amp, 0, Context[Index], &Other[Index])))
            Other[Index] = 0;
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Tas2764FaultNames); ++Index)
    {
        if (Latched & (1 << Index))
            DbgPrint("appleaudio: amp %s at %02x fault: %s\n", Amp->Name, Amp->Address, Tas2764FaultNames[Index]);
    }
    DbgPrint("appleaudio: amp %s at %02x fault context: %02x %02x %02x %02x %02x %02x\n", Amp->Name, Amp->Address,
             Latched, Other[0], Other[1], Other[2], Other[3], Other[4]);
    Tas2764Update(Amp, 0, TAS2764_INT_CLK_CFG, TAS2764_IRQZ_CLR, TAS2764_IRQZ_CLR);
    return Latched;
}

BOOLEAN
Tas2764ReadTemperature(
    _Inout_ PAPPLE_AUDIO_AMP Amp,
    _Out_ PLONG Celsius)
{
    UCHAR Value;

    *Celsius = 0;
    if (!NT_SUCCESS(Tas2764Read(Amp, 0, TAS2764_TEMP, &Value)) || !Value)
        return FALSE;
    *Celsius = (LONG)Value - TAS2764_TEMP_OFFSET;
    return TRUE;
}
