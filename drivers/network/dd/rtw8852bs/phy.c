/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Baseband and RF initialization, channel switching and coexistence
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright (c) 2019-2024 Realtek Corporation (BSD-3-Clause, see COPYING.rtw89)
 */

#include "rtw8852bs.h"
#include "phyreg.h"

#define NDEBUG
#include <debug.h>

#define RTW_ELEMENT_ALIGN               16
#define RTW_ELEMENT_HEADER_LENGTH       32
#define RTW_ELEMENT_BB_REG              2
#define RTW_ELEMENT_BB_GAIN             3
#define RTW_ELEMENT_RADIO_A             4
#define RTW_ELEMENT_RADIO_B             5
#define RTW_ELEMENT_RF_NCTL             8

#define RTW_PHY_HEADLINE_VALID          0xF
#define RTW_PHY_COND_BRANCH_IF          0x8
#define RTW_PHY_COND_BRANCH_ELIF        0x9
#define RTW_PHY_COND_BRANCH_ELSE        0xA
#define RTW_PHY_COND_BRANCH_END         0xB
#define RTW_PHY_COND_CHECK              0x4
#define RTW_PHY_COND_DONT_CARE          0xFF
#define RTW_RF_H2C_PAGE_SIZE            500
#define RTW_RF_H2C_PAGE_COUNT           3
#define RTW_H2C_CAT_OUTSRC              2
#define RTW_H2C_CL_OUTSRC_RF_REG_A      8
#define RTW_H2C_CL_OUTSRC_RF_REG_B      9

#define RTW_EFUSE_XTAL_K_OFFSET         0x2B9
#define RTW_EFUSE_RFE_TYPE_OFFSET       0x2CA
#define RTW_EFUSE_RX_GAIN_2G_OFDM       0x2D4
#define RTW_EFUSE_RX_GAIN_2G_CCK        0x2D6
#define RTW_EFUSE_RX_GAIN_5G_LOW        0x2D8
#define RTW_EFUSE_RX_GAIN_5G_MID        0x2DA
#define RTW_EFUSE_RX_GAIN_5G_HIGH       0x2DC

typedef struct _RTW_REG2
{
    ULONG Address;
    ULONG Data;
} RTW_REG2;

typedef VOID (*PRTW_REG_CONFIG)(PRTW_ADAPTER Adapter, ULONG Address, ULONG Data, UCHAR Path, PVOID Context);

typedef struct _RTW_RF_H2C
{
    ULONG Count;
    ULONG Entries[RTW_RF_H2C_PAGE_COUNT * RTW_RF_H2C_PAGE_SIZE];
} RTW_RF_H2C, *PRTW_RF_H2C;

static
ULONG
RtwMaskShift(
    _In_ ULONG Mask)
{
    ULONG Shift = 0;

    while (Shift < 32 && !(Mask & (1UL << Shift)))
        Shift++;
    return Shift;
}

ULONG
RtwBbRead32(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask)
{
    ULONG Value = RtwRead32(Adapter, Address | RTW_PHY_CR_BASE);

    return (Value & Mask) >> RtwMaskShift(Mask);
}

VOID
RtwBbWrite32(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    ULONG Current;

    if (Mask == 0xFFFFFFFF)
    {
        RtwWrite32(Adapter, Address | RTW_PHY_CR_BASE, Value);
        return;
    }

    Current = RtwRead32(Adapter, Address | RTW_PHY_CR_BASE);
    Current = (Current & ~Mask) | ((Value << RtwMaskShift(Mask)) & Mask);
    RtwWrite32(Adapter, Address | RTW_PHY_CR_BASE, Current);
}

static
BOOLEAN
RtwSwsiBusy(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Value = RtwRead32(Adapter, R_SWSI_V1 | RTW_PHY_CR_BASE);

    return (Value & (B_SWSI_W_BUSY_V1 | B_SWSI_R_BUSY_V1)) != 0;
}

static
BOOLEAN
RtwWaitSwsi(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Poll;

    for (Poll = 0; Poll < 30; Poll++)
    {
        if (!RtwSwsiBusy(Adapter))
            return TRUE;
        KeStallExecutionProcessor(1);
    }

    DPRINT1("RTW8852BS: RF serial interface busy\n");
    return FALSE;
}

ULONG
RtwRfRead(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Path,
    _In_ ULONG Address,
    _In_ ULONG Mask)
{
    static const ULONG Base[2] = { 0xE000, 0xF000 };
    ULONG Value, Poll;

    Mask &= RFREG_MASK;
    if (Address & RTW89_RF_ADDR_ADSEL_MASK)
        return RtwBbRead32(Adapter, Base[Path] + ((Address & 0xFF) << 2), Mask);

    if (!RtwWaitSwsi(Adapter))
        return RFREG_MASK;

    RtwBbWrite32(Adapter, R_SWSI_READ_ADDR_V1, B_SWSI_READ_ADDR_V1,
                 ((ULONG)Path << RtwMaskShift(B_SWSI_READ_ADDR_PATH_V1)) | (Address & B_SWSI_READ_ADDR_ADDR_V1));
    KeStallExecutionProcessor(2);

    for (Poll = 0; Poll < 30; Poll++)
    {
        Value = RtwRead32(Adapter, R_SWSI_V1 | RTW_PHY_CR_BASE);
        if (Value & B_SWSI_R_DATA_DONE_V1)
            return (Value & Mask) >> RtwMaskShift(Mask);
        KeStallExecutionProcessor(1);
    }

    DPRINT1("RTW8852BS: RF read %u/0x%lx timed out\n", Path, Address);
    return RFREG_MASK;
}

VOID
RtwRfWrite(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Path,
    _In_ ULONG Address,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    static const ULONG Base[2] = { 0xE000, 0xF000 };
    BOOLEAN Masked = FALSE;

    Mask &= RFREG_MASK;
    if (Address & RTW89_RF_ADDR_ADSEL_MASK)
    {
        RtwBbWrite32(Adapter, Base[Path] + ((Address & 0xFF) << 2), Mask, Value);
        KeStallExecutionProcessor(1);
        return;
    }

    if (!RtwWaitSwsi(Adapter))
        return;

    Value &= RFREG_MASK;
    if (Mask != RFREG_MASK)
    {
        Masked = TRUE;
        RtwBbWrite32(Adapter, R_SWSI_BIT_MASK_V1, RFREG_MASK, Mask);
        Value = (Value << RtwMaskShift(Mask)) & RFREG_MASK;
    }

    RtwBbWrite32(Adapter, R_SWSI_DATA_V1, 0xFFFFFFFF,
                 (Masked ? B_SWSI_DATA_BIT_MASK_EN_V1 : 0) |
                 ((ULONG)Path << RtwMaskShift(B_SWSI_DATA_PATH_V1)) |
                 ((Address & 0xFF) << RtwMaskShift(B_SWSI_DATA_ADDR_V1)) |
                 Value);
}

static
const UCHAR *
RtwFindElement(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Id,
    _In_ ULONG Index,
    _Out_ PULONG Count)
{
    const UCHAR *File = Adapter->Firmware;
    ULONG Offset, Size, Entries, Last;

    *Count = 0;
    Entries = File[1];
    if (Entries == 0)
        return NULL;

    Last = 16 + (Entries - 1) * 16;
    Offset = RtwLe32(File + Last + 4) + RtwLe32(File + Last + 8);
    Offset = (Offset + RTW_ELEMENT_ALIGN - 1) & ~(RTW_ELEMENT_ALIGN - 1);

    while (Offset + RTW_ELEMENT_HEADER_LENGTH < Adapter->FirmwareLength)
    {
        Size = RtwLe32(File + Offset + 4);
        if (Size > Adapter->FirmwareLength - Offset - RTW_ELEMENT_HEADER_LENGTH)
            break;

        if (RtwLe32(File + Offset) == Id &&
            (Id != RTW_ELEMENT_RADIO_A && Id != RTW_ELEMENT_RADIO_B ? TRUE : File[Offset + 24] == Index))
        {
            *Count = Size / sizeof(RTW_REG2);
            return File + Offset + RTW_ELEMENT_HEADER_LENGTH;
        }

        Offset += RTW_ELEMENT_HEADER_LENGTH + Size;
        Offset = (Offset + RTW_ELEMENT_ALIGN - 1) & ~(RTW_ELEMENT_ALIGN - 1);
    }

    return NULL;
}

const UCHAR *
RtwFindTxPowerElement(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Id,
    _Out_ PULONG EntrySize,
    _Out_ PULONG Count)
{
    const UCHAR *File = Adapter->Firmware, *Found = NULL;
    ULONG Offset, Size, Entries, Last;
    BOOLEAN Matched = FALSE;
    UCHAR Rfe;

    *EntrySize = 0;
    *Count = 0;
    Entries = File[1];
    if (Entries == 0)
        return NULL;

    Last = 16 + (Entries - 1) * 16;
    Offset = RtwLe32(File + Last + 4) + RtwLe32(File + Last + 8);
    Offset = (Offset + RTW_ELEMENT_ALIGN - 1) & ~(RTW_ELEMENT_ALIGN - 1);

    while (Offset + RTW_ELEMENT_HEADER_LENGTH < Adapter->FirmwareLength)
    {
        Size = RtwLe32(File + Offset + 4);
        if (Size > Adapter->FirmwareLength - Offset - RTW_ELEMENT_HEADER_LENGTH)
            break;

        if (RtwLe32(File + Offset) == Id && File[Offset + 27] != 0)
        {
            Rfe = File[Offset + 26];
            if (Rfe == Adapter->RfeType || (Rfe == 0 && !Matched))
            {
                Matched = Rfe == Adapter->RfeType;
                Found = File + Offset;
            }
        }

        Offset += RTW_ELEMENT_HEADER_LENGTH + Size;
        Offset = (Offset + RTW_ELEMENT_ALIGN - 1) & ~(RTW_ELEMENT_ALIGN - 1);
    }

    if (!Found)
        return NULL;

    *EntrySize = Found[27];
    *Count = min(RtwLe32(Found + 28), RtwLe32(Found + 4) / Found[27]);
    return Found + RTW_ELEMENT_HEADER_LENGTH;
}

static
BOOLEAN
RtwSelectHeadline(
    _In_reads_(Count) const UCHAR *Table,
    _In_ ULONG Count,
    _In_ UCHAR Rfe,
    _In_ UCHAR Cv,
    _Out_ PULONG HeadlineCount,
    _Out_ PULONG Target)
{
    ULONG i, Address, Compare, Best = MAXULONG;
    UCHAR CvMax = 0, RfePara, CvPara;

    for (i = 0; i < Count; i++)
    {
        if ((RtwLe32(Table + i * 8) >> 28) != RTW_PHY_HEADLINE_VALID)
            break;
    }
    *HeadlineCount = i;
    *Target = 0;
    if (i == 0)
        return TRUE;

    Compare = ((ULONG)Rfe << 16) | Cv;
    for (i = 0; i < *HeadlineCount; i++)
    {
        Address = RtwLe32(Table + i * 8);
        if ((Address & 0x0FFFFFFF) == Compare)
        {
            *Target = Address & 0x0FFFFFFF;
            return TRUE;
        }
    }

    Compare = ((ULONG)Rfe << 16) | RTW_PHY_COND_DONT_CARE;
    for (i = 0; i < *HeadlineCount; i++)
    {
        Address = RtwLe32(Table + i * 8);
        if ((Address & 0x0FFFFFFF) == Compare)
        {
            *Target = Address & 0x0FFFFFFF;
            return TRUE;
        }
    }

    for (i = 0; i < *HeadlineCount; i++)
    {
        Address = RtwLe32(Table + i * 8);
        RfePara = (UCHAR)(Address >> 16);
        CvPara = (UCHAR)Address;
        if (RfePara == Rfe && CvPara >= CvMax)
        {
            CvMax = CvPara;
            Best = i;
        }
    }
    if (Best == MAXULONG)
    {
        for (i = 0; i < *HeadlineCount; i++)
        {
            Address = RtwLe32(Table + i * 8);
            RfePara = (UCHAR)(Address >> 16);
            CvPara = (UCHAR)Address;
            if (RfePara == RTW_PHY_COND_DONT_CARE && CvPara >= CvMax)
            {
                CvMax = CvPara;
                Best = i;
            }
        }
    }
    if (Best == MAXULONG)
        return FALSE;

    *Target = RtwLe32(Table + Best * 8) & 0x0FFFFFFF;
    return TRUE;
}

static
NTSTATUS
RtwApplyTable(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG ElementId,
    _In_ UCHAR Path,
    _In_ PRTW_REG_CONFIG Config,
    _In_opt_ PVOID Context)
{
    const UCHAR *Table;
    ULONG Count, Headlines, CfgTarget, Target = 0, i, Address, Data, Applied = 0;
    BOOLEAN Matched = TRUE, Found = FALSE;
    UCHAR Condition;

    Table = RtwFindElement(Adapter, ElementId, Path, &Count);
    if (!Table)
    {
        DPRINT1("RTW8852BS: firmware element %lu/%u missing\n", ElementId, Path);
        return STATUS_NOT_FOUND;
    }

    if (!RtwSelectHeadline(Table, Count, Adapter->RfeType, Adapter->ChipVersion, &Headlines, &CfgTarget))
    {
        DPRINT1("RTW8852BS: no PHY package for RFE %u cut %u\n", Adapter->RfeType, Adapter->ChipVersion);
        return STATUS_NOT_SUPPORTED;
    }

    for (i = Headlines; i < Count; i++)
    {
        Address = RtwLe32(Table + i * 8);
        Data = RtwLe32(Table + i * 8 + 4);
        Condition = (UCHAR)(Address >> 28);

        switch (Condition)
        {
            case RTW_PHY_COND_BRANCH_IF:
            case RTW_PHY_COND_BRANCH_ELIF:
                Target = Address & 0x0FFFFFFF;
                break;
            case RTW_PHY_COND_BRANCH_ELSE:
                Matched = FALSE;
                if (!Found)
                {
                    DPRINT1("RTW8852BS: PHY table %lu has no matching branch\n", ElementId);
                    return STATUS_NOT_SUPPORTED;
                }
                break;
            case RTW_PHY_COND_BRANCH_END:
                Matched = TRUE;
                Found = FALSE;
                break;
            case RTW_PHY_COND_CHECK:
                if (Found)
                {
                    Matched = FALSE;
                    break;
                }
                Matched = Target == CfgTarget;
                Found = Matched;
                break;
            default:
                if (Matched)
                {
                    Config(Adapter, Address, Data, Path, Context);
                    Applied++;
                }
                break;
        }
    }

    DPRINT("RTW8852BS: table %lu/%u: %lu entries, %lu headlines, target 0x%07lx, applied %lu\n",
            ElementId, Path, Count, Headlines, CfgTarget, Applied);
    return STATUS_SUCCESS;
}

static
BOOLEAN
RtwConfigDelay(
    _In_ ULONG Address)
{
    switch (Address)
    {
        case 0xFE: RtwSleep(50); return TRUE;
        case 0xFD: RtwSleep(5); return TRUE;
        case 0xFC: RtwSleep(1); return TRUE;
        case 0xFB: KeStallExecutionProcessor(50); return TRUE;
        case 0xFA: KeStallExecutionProcessor(5); return TRUE;
        case 0xF9: KeStallExecutionProcessor(1); return TRUE;
        default: return FALSE;
    }
}

static
VOID
RtwConfigBb(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Data,
    _In_ UCHAR Path,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Context);

    if (RtwConfigDelay(Address) || Data == 0xDEADBEEF)
        return;
    RtwBbWrite32(Adapter, Address, 0xFFFFFFFF, Data);
}

static
VOID
RtwConfigBbGain(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Data,
    _In_ UCHAR Path,
    _In_opt_ PVOID Context)
{
    PRTW_BB_GAIN Gain = &Adapter->BbGain;
    UCHAR Type = (UCHAR)Address, Bw = (UCHAR)((Address >> 4) & 0xF), RxscStart = (UCHAR)(Address & 0xF);
    UCHAR GainPath = (UCHAR)(Address >> 8), Band = (UCHAR)(Address >> 16), CfgType = (UCHAR)(Address >> 24);
    ULONG i, Start, Lanes;

    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Context);

    if (Band >= RTW_BB_GAIN_BANDS || GainPath >= 2 || (Address >= 0xF9 && Address <= 0xFE))
        return;

    switch (CfgType)
    {
        case 0:
            if (Type == 0)
                for (i = 0; i < 4; i++, Data >>= 8) Gain->LnaGain[Band][GainPath][i] = (CHAR)Data;
            else if (Type == 1)
                for (i = 4; i < 7; i++, Data >>= 8) Gain->LnaGain[Band][GainPath][i] = (CHAR)Data;
            else if (Type == 2)
                for (i = 0; i < 2; i++, Data >>= 8) Gain->TiaGain[Band][GainPath][i] = (CHAR)Data;
            break;
        case 1:
            Start = 0;
            Lanes = 1;
            if (Bw == 0)
            {
                Gain->RplOfst20[Band][GainPath] = (CHAR)Data;
                break;
            }
            if (RxscStart == 1)
            {
                Start = 1;
                Lanes = Bw == 1 ? 2 : 4;
            }
            else if (RxscStart == 9)
            {
                Start = 9;
                Lanes = 2;
            }
            for (i = 0; i < Lanes; i++, Data >>= 8)
            {
                if (Bw == 1 && Start + i < RTW_RXSC_NUM_40)
                    Gain->RplOfst40[Band][GainPath][Start + i] = (CHAR)Data;
                else if (Bw == 2 && Start + i < RTW_RXSC_NUM_80)
                    Gain->RplOfst80[Band][GainPath][Start + i] = (CHAR)Data;
            }
            break;
        case 2:
            if (Type == 0)
                for (i = 0; i < 4; i++, Data >>= 8) Gain->LnaGainBypass[Band][GainPath][i] = (CHAR)Data;
            else if (Type == 1)
                for (i = 4; i < 7; i++, Data >>= 8) Gain->LnaGainBypass[Band][GainPath][i] = (CHAR)Data;
            break;
        case 3:
            if (Type == 0)
                for (i = 0; i < 4; i++, Data >>= 8) Gain->LnaOp1db[Band][GainPath][i] = (CHAR)Data;
            else if (Type == 1)
                for (i = 4; i < 7; i++, Data >>= 8) Gain->LnaOp1db[Band][GainPath][i] = (CHAR)Data;
            else if (Type == 2)
                for (i = 0; i < 4; i++, Data >>= 8) Gain->TiaLnaOp1db[Band][GainPath][i] = (CHAR)Data;
            else if (Type == 3)
                for (i = 4; i < 8; i++, Data >>= 8) Gain->TiaLnaOp1db[Band][GainPath][i] = (CHAR)Data;
            break;
        default:
            break;
    }
}

static
VOID
RtwConfigRf(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Data,
    _In_ UCHAR Path,
    _In_opt_ PVOID Context)
{
    PRTW_RF_H2C H2c = Context;

    if (Address == 0xFE)
    {
        RtwSleep(50);
        return;
    }

    RtwRfWrite(Adapter, Path, Address, RFREG_MASK, Data);

    if (Address < 0x100 || !H2c)
        return;
    if (H2c->Count >= RTL_NUMBER_OF(H2c->Entries))
    {
        DPRINT1("RTW8852BS: RF H2C table overflow\n");
        return;
    }
    H2c->Entries[H2c->Count++] = (Address << 20) | Data;
}

static
NTSTATUS
RtwSendRfRegisters(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Path,
    _In_ PRTW_RF_H2C H2c)
{
    ULONG Page, Offset, Length;
    NTSTATUS Status = STATUS_SUCCESS;

    for (Page = 0, Offset = 0; Offset < H2c->Count && Page < RTW_RF_H2C_PAGE_COUNT; Page++, Offset += Length)
    {
        Length = min(H2c->Count - Offset, (ULONG)RTW_RF_H2C_PAGE_SIZE);
        Status = RtwSendH2c(Adapter,
                            RTW_H2C_CAT_OUTSRC,
                            Path == 0 ? RTW_H2C_CL_OUTSRC_RF_REG_A : RTW_H2C_CL_OUTSRC_RF_REG_B,
                            (UCHAR)Page,
                            FALSE,
                            H2c->Entries + Offset,
                            Length * sizeof(ULONG));
        if (!NT_SUCCESS(Status))
            break;
    }

    H2c->Count = 0;
    return Status;
}

static
VOID
RtwResetBbAll(
    _In_ PRTW_ADAPTER Adapter)
{
    RtwBbWrite32(Adapter, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x7);
    RtwBbWrite32(Adapter, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG, 0x7);
    KeStallExecutionProcessor(1);
    RtwBbWrite32(Adapter, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1);
    RtwBbWrite32(Adapter, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0);
    RtwBbWrite32(Adapter, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x0);
    RtwBbWrite32(Adapter, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG, 0x0);
    RtwBbWrite32(Adapter, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1);
}

static
VOID
RtwBbReset(
    _In_ PRTW_ADAPTER Adapter)
{
    RtwBbWrite32(Adapter, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON, 1);
    RtwBbWrite32(Adapter, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN, 1);
    RtwBbWrite32(Adapter, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON, 1);
    RtwBbWrite32(Adapter, R_P1_TSSI_TRK, B_P1_TSSI_TRK_EN, 1);
    RtwResetBbAll(Adapter);
    RtwBbWrite32(Adapter, R_P0_TXPW_RSTB, B_P0_TXPW_RSTB_MANON, 0);
    RtwBbWrite32(Adapter, R_P0_TSSI_TRK, B_P0_TSSI_TRK_EN, 0);
    RtwBbWrite32(Adapter, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON, 0);
    RtwBbWrite32(Adapter, R_P1_TSSI_TRK, B_P1_TSSI_TRK_EN, 0);
}

static
NTSTATUS
RtwResetBbRf(
    _In_ PRTW_ADAPTER Adapter)
{
    UCHAR Value;
    NTSTATUS Status;

    RtwWrite32Clear(Adapter, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
    RtwWrite8(Adapter, R_AX_SYS_FUNC_EN, RtwRead8(Adapter, R_AX_SYS_FUNC_EN) & ~(B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN));
    Status = RtwReadXtalSi(Adapter, XTAL_SI_WL_RFC_S0, &Value);
    if (NT_SUCCESS(Status))
        Status = RtwWriteXtalSi(Adapter, XTAL_SI_WL_RFC_S0, Value & ~0x01, 0xFF);
    if (NT_SUCCESS(Status))
        Status = RtwReadXtalSi(Adapter, XTAL_SI_WL_RFC_S1, &Value);
    if (NT_SUCCESS(Status))
        Status = RtwWriteXtalSi(Adapter, XTAL_SI_WL_RFC_S1, Value & ~0x01, 0xFF);
    if (!NT_SUCCESS(Status))
        return Status;

    RtwWrite8(Adapter, R_AX_SYS_FUNC_EN, RtwRead8(Adapter, R_AX_SYS_FUNC_EN) | B_AX_FEN_BBRSTB | B_AX_FEN_BB_GLB_RSTN);
    RtwWrite32Mask(Adapter, R_AX_SPS_DIG_ON_CTRL0, B_AX_REG_ZCDC_H_MASK, 0x1);
    RtwWrite32Set(Adapter, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
    RtwWrite32Clear(Adapter, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
    RtwWrite32Set(Adapter, R_AX_WLRF_CTRL, B_AX_AFC_AFEDIG);
    Status = RtwWriteXtalSi(Adapter, XTAL_SI_WL_RFC_S0, 0xC7, 0xFF);
    if (NT_SUCCESS(Status))
        Status = RtwWriteXtalSi(Adapter, XTAL_SI_WL_RFC_S1, 0xC7, 0xFF);
    RtwWrite8(Adapter, R_AX_PHYREG_SET, PHYREG_SET_XYN_CYCLE);
    return Status;
}

static
VOID
RtwInitRfNctl(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Poll;

    RtwBbWrite32(Adapter, R_IOQ_IQK_DPK, 0xFFFFFFFF, RtwBbRead32(Adapter, R_IOQ_IQK_DPK, 0xFFFFFFFF) | 0x3);
    RtwBbWrite32(Adapter, R_GNT_BT_WGT_EN, 0xFFFFFFFF, RtwBbRead32(Adapter, R_GNT_BT_WGT_EN, 0xFFFFFFFF) | 0x1);
    RtwBbWrite32(Adapter, R_P0_PATH_RST, 0xFFFFFFFF, RtwBbRead32(Adapter, R_P0_PATH_RST, 0xFFFFFFFF) | 0x8000000);
    RtwBbWrite32(Adapter, R_P1_PATH_RST, 0xFFFFFFFF, RtwBbRead32(Adapter, R_P1_PATH_RST, 0xFFFFFFFF) | 0x8000000);
    RtwBbWrite32(Adapter, R_IOQ_IQK_DPK, 0xFFFFFFFF, RtwBbRead32(Adapter, R_IOQ_IQK_DPK, 0xFFFFFFFF) | 0x2);
    RtwBbWrite32(Adapter, R_NCTL_CFG, 0xFFFFFFFF, 0x8);

    for (Poll = 0; Poll < 100; Poll++)
    {
        RtwBbWrite32(Adapter, 0x8080, 0xFFFFFFFF, 0x4);
        KeStallExecutionProcessor(1);
        if (RtwBbRead32(Adapter, 0x8080, 0xFFFFFFFF) == 0x4)
            break;
        KeStallExecutionProcessor(10);
    }
    if (Poll == 100)
        DPRINT1("RTW8852BS: NCTL block did not respond\n");

    RtwApplyTable(Adapter, RTW_ELEMENT_RF_NCTL, 0, RtwConfigBb, NULL);
}

static
NTSTATUS
RtwWriteLte(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    ULONG Poll;

    for (Poll = 0; Poll < 1000; Poll++)
    {
        if (RtwRead8(Adapter, R_AX_LTE_CTRL + 3) & RTW_BIT(5))
            break;
        KeStallExecutionProcessor(50);
    }
    RtwWrite32(Adapter, R_AX_LTE_WDATA, Value);
    RtwWrite32(Adapter, R_AX_LTE_CTRL, 0xC00F0000 | Offset);
    return Poll < 1000 ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

static
ULONG
RtwReadLte(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    ULONG Poll;

    for (Poll = 0; Poll < 1000; Poll++)
    {
        if (RtwRead8(Adapter, R_AX_LTE_CTRL + 3) & RTW_BIT(5))
            break;
        KeStallExecutionProcessor(50);
    }
    RtwWrite32(Adapter, R_AX_LTE_CTRL, 0x800F0000 | Offset);
    return RtwRead32(Adapter, R_AX_LTE_RDATA);
}

static
VOID
RtwInitCoex(
    _In_ PRTW_ADAPTER Adapter)
{
    USHORT Cca;
    UCHAR Value;

    RtwWrite8(Adapter, R_AX_GPIO_MUXCFG, RtwRead8(Adapter, R_AX_GPIO_MUXCFG) | (UCHAR)B_AX_ENBT);
    RtwWrite8(Adapter, R_AX_BTC_FUNC_EN, RtwRead8(Adapter, R_AX_BTC_FUNC_EN) | (UCHAR)B_AX_PTA_WL_TX_EN);
    RtwWrite8(Adapter, R_AX_BT_COEX_CFG_2 + 1, RtwRead8(Adapter, R_AX_BT_COEX_CFG_2 + 1) | (UCHAR)(B_AX_GNT_BT_POLARITY >> 8));
    RtwWrite8(Adapter, R_AX_CSR_MODE, RtwRead8(Adapter, R_AX_CSR_MODE) | (UCHAR)(B_AX_STATIS_BT_EN | B_AX_WL_ACT_MSK));
    RtwWrite8(Adapter, R_AX_CSR_MODE + 2, RtwRead8(Adapter, R_AX_CSR_MODE + 2) | (UCHAR)(B_AX_BT_CNT_RST >> 16));
    RtwWrite8(Adapter, R_AX_TRXPTCL_RESP_0 + 3, RtwRead8(Adapter, R_AX_TRXPTCL_RESP_0 + 3) & ~(UCHAR)(B_AX_RSP_CHK_BTCCA >> 24));

    Cca = RtwRead16(Adapter, R_AX_CCA_CFG_0);
    Cca = (Cca | (USHORT)B_AX_BTCCA_EN) & ~(USHORT)B_AX_BTCCA_BRK_TXOP_EN;
    RtwWrite16(Adapter, R_AX_CCA_CFG_0, Cca);

    RtwWriteLte(Adapter, R_AX_LTE_SW_CFG_2, RtwReadLte(Adapter, R_AX_LTE_SW_CFG_2) & B_AX_WL_RX_CTRL);

    Value = RtwRead8(Adapter, R_AX_GPIO_MUXCFG);
    Value = (UCHAR)((Value & ~B_AX_BTMODE_MASK) | (MAC_AX_BT_MODE_0_3 << RtwMaskShift(B_AX_BTMODE_MASK)));
    RtwWrite8(Adapter, R_AX_GPIO_MUXCFG, Value);
    RtwWrite8(Adapter, R_AX_TDMA_MODE, RtwRead8(Adapter, R_AX_TDMA_MODE) | (UCHAR)B_AX_RTK_BT_ENABLE);
    Value = RtwRead8(Adapter, R_AX_BT_COEX_CFG_5);
    Value = (UCHAR)((Value & ~B_AX_BT_RPT_SAMPLE_RATE_MASK) | (MAC_AX_RTK_RATE << RtwMaskShift(B_AX_BT_RPT_SAMPLE_RATE_MASK)));
    RtwWrite8(Adapter, R_AX_BT_COEX_CFG_5, Value);

    Value = RtwRead8(Adapter, R_AX_GPIO_MUXCFG + 1);
    RtwWrite8(Adapter, R_AX_GPIO_MUXCFG + 1, (UCHAR)((Value & ~RTW_BIT(2)) | RTW_BIT(1)));

    RtwRfWrite(Adapter, 0, RR_WLSEL, RFREG_MASK, 0);
    RtwRfWrite(Adapter, 1, RR_WLSEL, RFREG_MASK, 0);
    RtwWrite32(Adapter, R_BTC_BREAK_TABLE, BTC_BREAK_PARAM);

    RtwWriteLte(Adapter, R_AX_LTE_SW_CFG_1,
                B_AX_GNT_BT_RFC_S0_SW_CTRL | B_AX_GNT_BT_BB_S0_SW_CTRL |
                B_AX_GNT_WL_RFC_S0_SW_VAL | B_AX_GNT_WL_BB_S0_SW_VAL |
                B_AX_GNT_WL_RFC_S0_SW_CTRL | B_AX_GNT_WL_BB_S0_SW_CTRL |
                B_AX_GNT_BT_RFC_S1_SW_CTRL | B_AX_GNT_BT_BB_S1_SW_CTRL |
                B_AX_GNT_WL_RFC_S1_SW_VAL | B_AX_GNT_WL_BB_S1_SW_VAL |
                B_AX_GNT_WL_RFC_S1_SW_CTRL | B_AX_GNT_WL_BB_S1_SW_CTRL);
}

static
UCHAR
RtwSubband(
    _In_ UCHAR Channel)
{
    if (Channel <= 14)
        return RTW_SUBBAND_2G;
    if (Channel <= 64)
        return RTW_SUBBAND_5G_1;
    if (Channel <= 144)
        return RTW_SUBBAND_5G_3;
    return RTW_SUBBAND_5G_4;
}

static
UCHAR
RtwGainBand(
    _In_ UCHAR Subband)
{
    switch (Subband)
    {
        case RTW_SUBBAND_5G_1: return 1;
        case RTW_SUBBAND_5G_3: return 2;
        case RTW_SUBBAND_5G_4: return 3;
        default: return 0;
    }
}

static
VOID
RtwSetGainError(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Subband,
    _In_ UCHAR Path)
{
    static const USHORT LnaG[7][2] =
    {
        { 0x4678, 0x475C }, { 0x4678, 0x475C }, { 0x467C, 0x4760 }, { 0x467C, 0x4760 },
        { 0x467C, 0x4760 }, { 0x467C, 0x4760 }, { 0x4680, 0x4764 },
    };
    static const USHORT LnaA[7][2] =
    {
        { 0x45DC, 0x4740 }, { 0x45DC, 0x4740 }, { 0x4660, 0x4744 }, { 0x4660, 0x4744 },
        { 0x4660, 0x4744 }, { 0x4660, 0x4744 }, { 0x4664, 0x4748 },
    };
    static const ULONG LnaMask[7] =
    {
        0x00FF0000, 0xFF000000, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000, 0x000000FF,
    };
    static const USHORT TiaG[2] = { 0x4680, 0x4764 };
    static const USHORT TiaA[2] = { 0x4664, 0x4748 };
    static const ULONG TiaMask[2] = { 0x00FF0000, 0xFF000000 };
    UCHAR Band = RtwGainBand(Subband);
    ULONG i;

    for (i = 0; i < 7; i++)
        RtwBbWrite32(Adapter, Subband == RTW_SUBBAND_2G ? LnaG[i][Path] : LnaA[i][Path], LnaMask[i],
                     (UCHAR)Adapter->BbGain.LnaGain[Band][Path][i]);
    for (i = 0; i < 2; i++)
        RtwBbWrite32(Adapter, Subband == RTW_SUBBAND_2G ? TiaG[Path] : TiaA[Path], TiaMask[i],
                     (UCHAR)Adapter->BbGain.TiaGain[Band][Path][i]);
}

static
LONG
RtwClamp(
    _In_ LONG Value,
    _In_ LONG Low,
    _In_ LONG High)
{
    return Value < Low ? Low : (Value > High ? High : Value);
}

static
VOID
RtwSetGainOffset(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Subband)
{
    static const ULONG RssiOffsetAddress[2] = { R_PATH0_G_TIA1_LNA6_OP1DB_V1, R_PATH1_G_TIA1_LNA6_OP1DB_V1 };
    UCHAR OfdmBand;
    LONG OffsetA, OffsetB, OffsetOfdm, OffsetCck, Value;

    if (!Adapter->EfuseGainValid)
        return;

    OfdmBand = Subband == RTW_SUBBAND_2G ? 1 : (Subband == RTW_SUBBAND_5G_1 ? 2 : (Subband == RTW_SUBBAND_5G_3 ? 3 : 4));
    OffsetA = -Adapter->EfuseGainOffset[0][OfdmBand];
    OffsetB = -Adapter->EfuseGainOffset[1][OfdmBand];

    Value = RtwClamp(-((OffsetA << 2) + (Adapter->GainOffsetBase >> 2)), -128, 127);
    RtwBbWrite32(Adapter, RssiOffsetAddress[0], B_PATH0_R_G_OFST_MASK, (UCHAR)Value);
    Value = RtwClamp(-((OffsetB << 2) + (Adapter->GainOffsetBase >> 2)), -128, 127);
    RtwBbWrite32(Adapter, RssiOffsetAddress[1], B_PATH0_R_G_OFST_MASK, (UCHAR)Value);

    OffsetOfdm = -Adapter->EfuseGainOffset[0][OfdmBand];
    OffsetCck = -Adapter->EfuseGainOffset[0][0];

    Value = RtwClamp((OffsetOfdm << 4) + Adapter->GainOffsetBase, -128, 127);
    RtwBbWrite32(Adapter, R_P0_RPL1, B_P0_RPL1_BIAS_MASK, (ULONG)Value & 0xFF);
    Value = RtwClamp((OffsetOfdm << 4) + Adapter->RssiBase, -128, 127);
    RtwBbWrite32(Adapter, R_P1_RPL1, B_P0_RPL1_BIAS_MASK, (ULONG)Value & 0xFF);

    if (Subband == RTW_SUBBAND_2G)
    {
        Value = RtwClamp((OffsetCck << 3) + (Adapter->GainOffsetBase >> 1), -64, 63);
        RtwBbWrite32(Adapter, R_RX_RPL_OFST, B_RX_RPL_OFST_CCK_MASK, (ULONG)Value & 0x7F);
    }
}

static
VOID
RtwSetRxscRplComp(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Subband)
{
    PRTW_BB_GAIN Gain = &Adapter->BbGain;
    UCHAR Band = RtwGainBand(Subband);
    ULONG Value;

#define RTW_AVG2(a, b) ((ULONG)(UCHAR)(((LONG)(a) + (LONG)(b)) >> 1))
    Value = (RTW_AVG2(Gain->RplOfst20[Band][0], Gain->RplOfst20[Band][1]) << RtwMaskShift(B_P0_RPL1_20_MASK)) |
            (RTW_AVG2(Gain->RplOfst40[Band][0][0], Gain->RplOfst40[Band][1][0]) << RtwMaskShift(B_P0_RPL1_40_MASK)) |
            (RTW_AVG2(Gain->RplOfst40[Band][0][1], Gain->RplOfst40[Band][1][1]) << RtwMaskShift(B_P0_RPL1_41_MASK));
    Value >>= B_P0_RPL1_SHIFT;
    RtwBbWrite32(Adapter, R_P0_RPL1, B_P0_RPL1_MASK, Value);
    RtwBbWrite32(Adapter, R_P1_RPL1, B_P0_RPL1_MASK, Value);

    Value = (RTW_AVG2(Gain->RplOfst40[Band][0][2], Gain->RplOfst40[Band][1][2]) << RtwMaskShift(B_P0_RTL2_42_MASK)) |
            (RTW_AVG2(Gain->RplOfst80[Band][0][0], Gain->RplOfst80[Band][1][0]) << RtwMaskShift(B_P0_RTL2_80_MASK)) |
            (RTW_AVG2(Gain->RplOfst80[Band][0][1], Gain->RplOfst80[Band][1][1]) << RtwMaskShift(B_P0_RTL2_81_MASK)) |
            (RTW_AVG2(Gain->RplOfst80[Band][0][10], Gain->RplOfst80[Band][1][10]) << RtwMaskShift(B_P0_RTL2_8A_MASK));
    RtwBbWrite32(Adapter, R_P0_RPL2, 0xFFFFFFFF, Value);
    RtwBbWrite32(Adapter, R_P1_RPL2, 0xFFFFFFFF, Value);

    Value = (RTW_AVG2(Gain->RplOfst80[Band][0][2], Gain->RplOfst80[Band][1][2]) << RtwMaskShift(B_P0_RTL3_82_MASK)) |
            (RTW_AVG2(Gain->RplOfst80[Band][0][3], Gain->RplOfst80[Band][1][3]) << RtwMaskShift(B_P0_RTL3_83_MASK)) |
            (RTW_AVG2(Gain->RplOfst80[Band][0][4], Gain->RplOfst80[Band][1][4]) << RtwMaskShift(B_P0_RTL3_84_MASK)) |
            (RTW_AVG2(Gain->RplOfst80[Band][0][9], Gain->RplOfst80[Band][1][9]) << RtwMaskShift(B_P0_RTL3_89_MASK));
    RtwBbWrite32(Adapter, R_P0_RPL3, 0xFFFFFFFF, Value);
    RtwBbWrite32(Adapter, R_P1_RPL3, 0xFFFFFFFF, Value);
#undef RTW_AVG2
}

static
UCHAR
RtwScoMapping(
    _In_ UCHAR Channel)
{
    if (Channel == 1) return 109;
    if (Channel <= 6) return 108;
    if (Channel <= 10) return 107;
    if (Channel <= 14) return 106;
    if (Channel == 36 || Channel == 38) return 51;
    if (Channel >= 40 && Channel <= 58) return 50;
    if (Channel >= 60 && Channel <= 64) return 49;
    if (Channel == 100 || Channel == 102) return 48;
    if (Channel >= 104 && Channel <= 126) return 47;
    if (Channel >= 128 && Channel <= 151) return 46;
    if (Channel >= 153 && Channel <= 177) return 45;
    return 0;
}

static
UCHAR
RtwEncodeChannelIndex(
    _In_ UCHAR Channel)
{
    static const UCHAR Base5g[] = { 36, 100, 132, 149 };
    LONG Index;

    if (Channel <= 14)
        return Channel & 0x0F;

    for (Index = RTL_NUMBER_OF(Base5g) - 1; Index >= 0; Index--)
    {
        if (Channel >= Base5g[Index])
            return (UCHAR)(((Index + 2) << 4) | (((Channel - Base5g[Index]) >> 1) & 0x0F));
    }
    return 0;
}

static
VOID
RtwSetChannelMac(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel)
{
    RtwWrite8(Adapter, R_AX_WMAC_RFMOD, RtwRead8(Adapter, R_AX_WMAC_RFMOD) & ~(UCHAR)B_AX_WMAC_RFMOD_MASK);
    RtwWrite32(Adapter, R_AX_TX_SUB_CARRIER_VALUE, 0);

    if (Channel > 14)
        RtwWrite8(Adapter, R_AX_TXRATE_CHK,
                  (RtwRead8(Adapter, R_AX_TXRATE_CHK) & ~(UCHAR)B_AX_BAND_MODE) | (UCHAR)(B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6));
    else
        RtwWrite8(Adapter, R_AX_TXRATE_CHK,
                  (RtwRead8(Adapter, R_AX_TXRATE_CHK) | (UCHAR)B_AX_BAND_MODE) & ~(UCHAR)(B_AX_CHECK_CCK_EN | B_AX_RTS_LIMIT_IN_OFDM6));
}

static
VOID
RtwCtrlBtgBtRx(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Enable)
{
    RtwBbWrite32(Adapter, R_PATH0_BT_SHARE_V1, B_PATH0_BT_SHARE_V1, Enable ? 1 : 0);
    RtwBbWrite32(Adapter, R_PATH0_BTG_PATH_V1, B_PATH0_BTG_PATH_V1, 0);
    RtwBbWrite32(Adapter, R_PATH1_G_LNA6_OP1DB_V1, B_PATH1_G_LNA6_OP1DB_V1, Enable ? 0x20 : 0x1A);
    RtwBbWrite32(Adapter, R_PATH1_G_TIA0_LNA6_OP1DB_V1, B_PATH1_G_TIA0_LNA6_OP1DB_V1, Enable ? 0x30 : 0x2A);
    RtwBbWrite32(Adapter, R_PATH1_BT_SHARE_V1, B_PATH1_BT_SHARE_V1, Enable ? 1 : 0);
    RtwBbWrite32(Adapter, R_PATH1_BTG_PATH_V1, B_PATH1_BTG_PATH_V1, Enable ? 1 : 0);
    RtwBbWrite32(Adapter, R_PMAC_GNT, B_PMAC_GNT_P1, Enable ? 0x0 : 0xC);
    RtwBbWrite32(Adapter, R_CHBW_MOD_V1, B_BT_SHARE, Enable ? 1 : 0);
    RtwBbWrite32(Adapter, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, Enable ? 2 : 0);
    RtwBbWrite32(Adapter, R_BT_DYN_DC_EST_EN_V1, B_BT_DYN_DC_EST_EN_MSK, 1);
    RtwBbWrite32(Adapter, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, Enable ? 1 : 0);
}

static
VOID
RtwSetChannelBb(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel)
{
    static const ULONG Barker[14] =
    {
        0x1cfea, 0x1d0e1, 0x1d1d7, 0x1d2cd, 0x1d3c3, 0x1d4b9, 0x1d5b0, 0x1d6a6,
        0x1d79c, 0x1d892, 0x1d988, 0x1da7f, 0x1db75, 0x1ddc4
    };
    static const ULONG Cck[14] =
    {
        0x27de3, 0x27f35, 0x28088, 0x281da, 0x2832d, 0x2847f, 0x285d2, 0x28724,
        0x28877, 0x289c9, 0x28b1c, 0x28c6e, 0x28dc1, 0x290ed
    };
    BOOLEAN Is2g = Channel <= 14;
    UCHAR Subband = RtwSubband(Channel);
    ULONG RxPath;

    if (Is2g)
    {
        RtwBbWrite32(Adapter, R_RXSCOBC, B_RXSCOBC_TH, Barker[Channel - 1]);
        RtwBbWrite32(Adapter, R_RXSCOCCK, B_RXSCOCCK_TH, Cck[Channel - 1]);
    }

    RtwBbWrite32(Adapter, R_PATH0_BAND_SEL_V1, B_PATH0_BAND_SEL_MSK_V1, Is2g ? 1 : 0);
    RtwBbWrite32(Adapter, R_PATH1_BAND_SEL_V1, B_PATH1_BAND_SEL_MSK_V1, Is2g ? 1 : 0);
    RtwBbWrite32(Adapter, R_FC0_BW_V1, B_FC0_BW_INV, RtwScoMapping(Channel));

    if (Channel == 14)
    {
        RtwBbWrite32(Adapter, R_TXFIR0, B_TXFIR_C01, 0x3b13ff);
        RtwBbWrite32(Adapter, R_TXFIR2, B_TXFIR_C23, 0x1c42de);
        RtwBbWrite32(Adapter, R_TXFIR4, B_TXFIR_C45, 0xfdb0ad);
        RtwBbWrite32(Adapter, R_TXFIR6, B_TXFIR_C67, 0xf60f6e);
        RtwBbWrite32(Adapter, R_TXFIR8, B_TXFIR_C89, 0xfd8f92);
        RtwBbWrite32(Adapter, R_TXFIRA, B_TXFIR_CAB, 0x2d011);
        RtwBbWrite32(Adapter, R_TXFIRC, B_TXFIR_CCD, 0x1c02c);
        RtwBbWrite32(Adapter, R_TXFIRE, B_TXFIR_CEF, 0xfff00a);
    }
    else
    {
        RtwBbWrite32(Adapter, R_TXFIR0, B_TXFIR_C01, 0x3d23ff);
        RtwBbWrite32(Adapter, R_TXFIR2, B_TXFIR_C23, 0x29b354);
        RtwBbWrite32(Adapter, R_TXFIR4, B_TXFIR_C45, 0xfc1c8);
        RtwBbWrite32(Adapter, R_TXFIR6, B_TXFIR_C67, 0xfdb053);
        RtwBbWrite32(Adapter, R_TXFIR8, B_TXFIR_C89, 0xf86f9a);
        RtwBbWrite32(Adapter, R_TXFIRA, B_TXFIR_CAB, 0xfaef92);
        RtwBbWrite32(Adapter, R_TXFIRC, B_TXFIR_CCD, 0xfe5fcc);
        RtwBbWrite32(Adapter, R_TXFIRE, B_TXFIR_CEF, 0xffdff5);
    }

    RtwSetGainError(Adapter, Subband, 0);
    RtwSetGainError(Adapter, Subband, 1);
    RtwSetGainOffset(Adapter, Subband);
    RtwSetRxscRplComp(Adapter, Subband);

    RxPath = RtwBbRead32(Adapter, R_CHBW_MOD_V1, B_ANT_RX_SEG0);
    RtwBbWrite32(Adapter, R_FC0_BW_V1, B_FC0_BW_SET, 0x0);
    RtwBbWrite32(Adapter, R_CHBW_MOD_V1, B_CHBW_MOD_SBW, 0x0);
    RtwBbWrite32(Adapter, R_CHBW_MOD_V1, B_CHBW_MOD_PRICH, 0x0);
    RtwBbWrite32(Adapter, R_P0_RFMODE_ORI_RX, B_P0_RFMODE_ORI_RX_ALL, 0x333);
    RtwBbWrite32(Adapter, R_P1_RFMODE_ORI_RX, B_P1_RFMODE_ORI_RX_ALL, 0x333);
    RtwBbWrite32(Adapter, 0xC0EC, 0x6000, 0x0);
    RtwBbWrite32(Adapter, 0xC0E4, 0x30, 0x2);
    RtwBbWrite32(Adapter, 0xC1EC, 0x6000, 0x0);
    RtwBbWrite32(Adapter, 0xC1E4, 0x30, 0x2);
    if (RxPath == 0x1)
        RtwBbWrite32(Adapter, R_P1_RFMODE_ORI_RX, B_P1_RFMODE_ORI_RX_ALL, 0x111);
    else if (RxPath == 0x2)
        RtwBbWrite32(Adapter, R_P0_RFMODE_ORI_RX, B_P0_RFMODE_ORI_RX_ALL, 0x111);

    RtwBbWrite32(Adapter, R_UPD_CLK_ADC, B_ENABLE_CCK, Is2g ? 1 : 0);
    RtwBbWrite32(Adapter, R_RXCCA, B_RXCCA_DIS, Is2g ? 0 : 1);

    if (!Is2g)
    {
        RtwBbWrite32(Adapter, R_PATH0_BT_SHARE_V1, B_PATH0_BT_SHARE_V1, 0x0);
        RtwBbWrite32(Adapter, R_PATH0_BTG_PATH_V1, B_PATH0_BTG_PATH_V1, 0x0);
        RtwBbWrite32(Adapter, R_PATH1_BT_SHARE_V1, B_PATH1_BT_SHARE_V1, 0x0);
        RtwBbWrite32(Adapter, R_PATH1_BTG_PATH_V1, B_PATH1_BTG_PATH_V1, 0x0);
        RtwBbWrite32(Adapter, R_CHBW_MOD_V1, B_BT_SHARE, 0x0);
        RtwBbWrite32(Adapter, R_FC0_BW_V1, B_ANT_RX_BT_SEG0, 0x0);
        RtwBbWrite32(Adapter, R_BT_DYN_DC_EST_EN_V1, B_BT_DYN_DC_EST_EN_MSK, 0x0);
        RtwBbWrite32(Adapter, R_GNT_BT_WGT_EN, B_GNT_BT_WGT_EN, 0x0);
    }

    RtwBbWrite32(Adapter, R_MAC_PIN_SEL, B_CH_IDX_SEG0, RtwEncodeChannelIndex(Channel));
    RtwBbWrite32(Adapter, R_PATH0_5MDET_V1, B_PATH0_5MDET_EN, 0x0);
    RtwBbWrite32(Adapter, R_PATH1_5MDET_V1, B_PATH1_5MDET_EN, 0x0);
    RtwBbWrite32(Adapter, R_ASSIGN_SBD_OPT_V1, B_ASSIGN_SBD_OPT_EN_V1, 0x0);
    RtwResetBbAll(Adapter);
}

static
BOOLEAN
RtwSetS0Arfc18(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Value)
{
    ULONG Backup, Poll;

    Backup = RtwRfRead(Adapter, 0, RR_LDO, RFREG_MASK);
    RtwRfWrite(Adapter, 0, RR_LDO, RR_LDO_SEL, 0x1);
    RtwRfWrite(Adapter, 0, RR_CFGCH, RFREG_MASK, Value);

    for (Poll = 0; Poll < 1000; Poll++)
    {
        if (RtwRfRead(Adapter, 0, RR_LPF, RR_LPF_BUSY) == 0)
            break;
        KeStallExecutionProcessor(1);
    }

    RtwRfWrite(Adapter, 0, RR_LDO, RFREG_MASK, Backup);
    return Poll == 1000;
}

static
VOID
RtwLckCheck(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Value;

    if (RtwRfRead(Adapter, 0, RR_SYNFB, RR_SYNFB_LK) == 0)
    {
        RtwRfWrite(Adapter, 0, RR_MMD, RR_MMD_RST_EN, 0x1);
        RtwRfWrite(Adapter, 0, RR_MMD, RR_MMD_RST_SYN, 0x0);
        RtwRfWrite(Adapter, 0, RR_MMD, RR_MMD_RST_SYN, 0x1);
        RtwRfWrite(Adapter, 0, RR_MMD, RR_MMD_RST_EN, 0x0);
    }

    KeStallExecutionProcessor(10);

    if (RtwRfRead(Adapter, 0, RR_SYNFB, RR_SYNFB_LK) == 0)
    {
        RtwRfWrite(Adapter, 0, RR_LCK_TRG, RR_LCK_TRGSEL, 0x1);
        Value = RtwRfRead(Adapter, 0, RR_CFGCH, RFREG_MASK);
        RtwSetS0Arfc18(Adapter, Value);
        RtwRfWrite(Adapter, 0, RR_LCK_TRG, RR_LCK_TRGSEL, 0x0);
    }

    if (RtwRfRead(Adapter, 0, RR_SYNFB, RR_SYNFB_LK) == 0)
    {
        Value = RtwRfRead(Adapter, 0, RR_POW, RFREG_MASK);
        RtwRfWrite(Adapter, 0, RR_POW, RFREG_MASK, Value);
        Value = RtwRfRead(Adapter, 0, RR_SX, RFREG_MASK);
        RtwRfWrite(Adapter, 0, RR_SX, RFREG_MASK, Value);
        RtwRfWrite(Adapter, 0, RR_SYNLUT, RR_SYNLUT_MOD, 0x1);
        RtwRfWrite(Adapter, 0, RR_POW, RR_POW_SYN, 0x0);
        RtwRfWrite(Adapter, 0, RR_POW, RR_POW_SYN, 0x3);
        RtwRfWrite(Adapter, 0, RR_SYNLUT, RR_SYNLUT_MOD, 0x0);
        RtwRfWrite(Adapter, 0, RR_LCK_TRG, RR_LCK_TRGSEL, 0x1);
        Value = RtwRfRead(Adapter, 0, RR_CFGCH, RFREG_MASK);
        RtwSetS0Arfc18(Adapter, Value);
        RtwRfWrite(Adapter, 0, RR_LCK_TRG, RR_LCK_TRGSEL, 0x0);
        DPRINT1("RTW8852BS: synthesizer relock 0xb2=%05lx 0xc5=%05lx\n",
                RtwRfRead(Adapter, 0, RR_VCO, RFREG_MASK), RtwRfRead(Adapter, 0, RR_SYNFB, RFREG_MASK));
    }
}

static
VOID
RtwRfChannelSetting(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Path,
    _In_ UCHAR Channel,
    _In_ BOOLEAN Dav)
{
    ULONG Address = Dav ? RR_CFGCH : RR_CFGCH_V1;
    ULONG Value;

    Value = RtwRfRead(Adapter, Path, Address, RFREG_MASK);
    Value &= ~(RR_CFGCH_BAND1 | RR_CFGCH_POW_LCK | RR_CFGCH_TRX_AH | RR_CFGCH_BCN | RR_CFGCH_BAND0 | RR_CFGCH_CH);
    Value |= Channel;
    if (Channel > 14)
        Value |= (CFGCH_BAND1_5G << RtwMaskShift(RR_CFGCH_BAND1)) | (CFGCH_BAND0_5G << RtwMaskShift(RR_CFGCH_BAND0));
    Value &= ~(RR_CFGCH_POW_LCK | RR_CFGCH_TRX_AH | RR_CFGCH_BCN | RR_CFGCH_BW2) & RFREG_MASK;
    Value |= RR_CFGCH_BW2;

    if (Path == 0 && Dav)
    {
        if (!RtwSetS0Arfc18(Adapter, Value))
            RtwLckCheck(Adapter);
    }
    else
    {
        RtwRfWrite(Adapter, Path, Address, RFREG_MASK, Value);
    }

    RtwRfWrite(Adapter, Path, RR_LCKST, RR_LCKST_BIN, 0);
    RtwRfWrite(Adapter, Path, RR_LCKST, RR_LCKST_BIN, 1);
}

static
VOID
RtwRfBandwidthSetting(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Path,
    _In_ BOOLEAN Dav)
{
    ULONG Address = Dav ? RR_CFGCH : RR_CFGCH_V1;
    ULONG Value;

    Value = RtwRfRead(Adapter, Path, Address, RFREG_MASK);
    if (Value == RFREG_MASK)
        return;

    Value &= ~RR_CFGCH_BW;
    Value |= CFGCH_BW_20M << RtwMaskShift(RR_CFGCH_BW);
    Value &= ~(RR_CFGCH_POW_LCK | RR_CFGCH_TRX_AH | RR_CFGCH_BCN | RR_CFGCH_BW2) & RFREG_MASK;
    Value |= RR_CFGCH_BW2;
    RtwRfWrite(Adapter, Path, Address, RFREG_MASK, Value);
}

static
VOID
RtwSetChannelRf(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel)
{
    UCHAR Path;

    RtwRfChannelSetting(Adapter, 0, Channel, TRUE);
    RtwRfChannelSetting(Adapter, 1, Channel, TRUE);
    RtwRfChannelSetting(Adapter, 0, Channel, FALSE);
    RtwRfChannelSetting(Adapter, 1, Channel, FALSE);

    RtwRfBandwidthSetting(Adapter, 0, TRUE);
    RtwRfBandwidthSetting(Adapter, 1, TRUE);
    RtwRfBandwidthSetting(Adapter, 0, FALSE);
    RtwRfBandwidthSetting(Adapter, 1, FALSE);

    for (Path = 0; Path < 2; Path++)
    {
        RtwRfWrite(Adapter, Path, RR_LUTWE2, RR_LUTWE2_RTXBW, 0x1);
        RtwRfWrite(Adapter, Path, RR_LUTWA, RR_LUTWA_M2, 0x12);
        RtwRfWrite(Adapter, Path, RR_LUTWD0, RR_LUTWD0_LB, 0x1B);
        RtwRfWrite(Adapter, Path, RR_LUTWE2, RR_LUTWE2_RTXBW, 0x0);
    }
}

static
VOID
RtwChannelHelp(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Enter,
    _In_ UCHAR Channel)
{
    UCHAR Path;

    if (Enter)
    {
        RtwWrite32Clear(Adapter, R_AX_PPDU_STAT, B_AX_PPDU_STAT_RPT_EN);
        for (Path = 0; Path < 2; Path++)
        {
            RtwBbWrite32(Adapter, Path == 0 ? R_P0_TXPW_RSTB : R_P1_TXPW_RSTB, B_P0_TXPW_RSTB_MANON, 0x1);
            RtwBbWrite32(Adapter, Path == 0 ? R_P0_TSSI_TRK : R_P1_TSSI_TRK, B_P0_TSSI_TRK_EN, 0x1);
        }
        RtwBbWrite32(Adapter, R_ADC_FIFO, B_ADC_FIFO_RST, 0xF);
        KeStallExecutionProcessor(40);
        RtwBbWrite32(Adapter, R_RXCCA, B_RXCCA_DIS, 0x1);
        RtwBbWrite32(Adapter, R_PD_CTRL, B_PD_HIT_DIS, 0x1);
        RtwBbWrite32(Adapter, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x7);
        RtwBbWrite32(Adapter, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG, 0x7);
        KeStallExecutionProcessor(1);
        RtwBbWrite32(Adapter, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0);
        return;
    }

    RtwBbWrite32(Adapter, R_ADC_FIFO, B_ADC_FIFO_RST, 0x0);
    for (Path = 0; Path < 2; Path++)
    {
        RtwBbWrite32(Adapter, Path == 0 ? R_P0_TXPW_RSTB : R_P1_TXPW_RSTB, B_P0_TXPW_RSTB_MANON, 0x0);
        RtwBbWrite32(Adapter, Path == 0 ? R_P0_TSSI_TRK : R_P1_TSSI_TRK, B_P0_TSSI_TRK_EN, 0x0);
    }
    RtwBbWrite32(Adapter, R_S0_HW_SI_DIS, B_S0_HW_SI_DIS_W_R_TRIG, 0x0);
    RtwBbWrite32(Adapter, R_S1_HW_SI_DIS, B_S1_HW_SI_DIS_W_R_TRIG, 0x0);
    RtwBbWrite32(Adapter, R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1);
    if (Channel <= 14)
        RtwBbWrite32(Adapter, R_RXCCA, B_RXCCA_DIS, 0x0);
    RtwBbWrite32(Adapter, R_PD_CTRL, B_PD_HIT_DIS, 0x0);
    RtwWrite32(Adapter, R_AX_PPDU_STAT,
               B_AX_PPDU_STAT_RPT_EN | B_AX_APP_MAC_INFO_RPT | B_AX_APP_PLCP_HDR_RPT | B_AX_PPDU_STAT_RPT_CRC32);
    RtwWrite32Mask(Adapter, R_AX_HW_RPT_FWD, B_AX_FWD_PPDU_STAT_MASK, RTW_PRPT_DEST_HOST);
}

VOID
RtwSetChannel(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel)
{
    RtwChannelHelp(Adapter, TRUE, Channel);
    RtwSetChannelMac(Adapter, Channel);
    RtwSetChannelBb(Adapter, Channel);
    RtwSetChannelRf(Adapter, Channel);
    RtwChannelHelp(Adapter, FALSE, Channel);
    Adapter->CurrentChannel = Channel;
    RtwSetTxPower(Adapter, Channel);
}

static
VOID
RtwCfgTrxPath(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel)
{
    RtwBbWrite32(Adapter, R_CHBW_MOD_V1, B_ANT_RX_SEG0, 3);
    RtwBbWrite32(Adapter, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG0, 3);
    RtwBbWrite32(Adapter, R_FC0_BW_V1, B_ANT_RX_1RCCA_SEG1, 3);
    RtwBbWrite32(Adapter, R_RXHT_MCS_LIMIT, B_RXHT_MCS_LIMIT, 1);
    RtwBbWrite32(Adapter, R_RXVHT_MCS_LIMIT, B_RXVHT_MCS_LIMIT, 1);
    RtwBbWrite32(Adapter, R_RXHE, B_RXHE_USER_MAX, 4);
    RtwBbWrite32(Adapter, R_RXHE, B_RXHE_MAX_NSS, 1);
    RtwBbWrite32(Adapter, R_RXHE, B_RXHETB_MAX_NSS, 1);

    RtwSetGainOffset(Adapter, RtwSubband(Channel));
    RtwCtrlBtgBtRx(Adapter, Channel <= 14);

    RtwBbWrite32(Adapter, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON | B_P1_TXPW_RSTB_TSSI, 1);
    RtwBbWrite32(Adapter, R_P1_TXPW_RSTB, B_P1_TXPW_RSTB_MANON | B_P1_TXPW_RSTB_TSSI, 3);

    RtwBbWrite32(Adapter, R_P0_RFMODE, B_P0_RFMODE_ORI_TXRX_FTM_TX, 0x1233312);
    RtwBbWrite32(Adapter, R_P0_RFMODE_FTM_RX, B_P0_RFMODE_FTM_RX, 0x333);
    RtwBbWrite32(Adapter, R_P1_RFMODE, B_P1_RFMODE_ORI_TXRX_FTM_TX, 0x1233312);
    RtwBbWrite32(Adapter, R_P1_RFMODE_FTM_RX, B_P1_RFMODE_FTM_RX, 0x333);

    RtwBbWrite32(Adapter, R_MAC_SEL, B_MAC_SEL_MOD, 0x0);
}

static
VOID
RtwParsePhyEfuse(
    _In_ PRTW_ADAPTER Adapter)
{
    static const ULONG GainAddress[5] =
    {
        RTW_EFUSE_RX_GAIN_2G_CCK, RTW_EFUSE_RX_GAIN_2G_OFDM, RTW_EFUSE_RX_GAIN_5G_LOW,
        RTW_EFUSE_RX_GAIN_5G_MID, RTW_EFUSE_RX_GAIN_5G_HIGH,
    };
    UCHAR Value;
    ULONG i;

    Adapter->RfeType = Adapter->LogicalEfuse[RTW_EFUSE_RFE_TYPE_OFFSET];
    Adapter->XtalCap = Adapter->LogicalEfuse[RTW_EFUSE_XTAL_K_OFFSET];
    Adapter->EfuseGainValid = FALSE;
    for (i = 0; i < RTL_NUMBER_OF(GainAddress); i++)
    {
        Value = Adapter->LogicalEfuse[GainAddress[i]];
        Adapter->EfuseGainOffset[0][i] = (CHAR)((CHAR)Value >> 4);
        Adapter->EfuseGainOffset[1][i] = (CHAR)((CHAR)(Value << 4) >> 4);
        if (Value != 0xFF)
            Adapter->EfuseGainValid = TRUE;
    }
}

NTSTATUS
RtwPhyInit(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel)
{
    PRTW_RF_H2C H2c;
    NTSTATUS Status;
    ULONG Address;
    UCHAR Path;

    RtwParsePhyEfuse(Adapter);

    Status = RtwResetBbRf(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtwApplyTable(Adapter, RTW_ELEMENT_BB_REG, 0, RtwConfigBb, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    RtwWrite32(Adapter, R_AX_PWR_UL_CTRL2, 0x07763333);
    RtwWrite32(Adapter, R_AX_PWR_COEXT_CTRL, 0x01ebf000);
    RtwWrite32(Adapter, R_AX_PWR_UL_CTRL0, 0x0002f8ff);

    RtlZeroMemory(&Adapter->BbGain, sizeof(Adapter->BbGain));
    Status = RtwApplyTable(Adapter, RTW_ELEMENT_BB_GAIN, 0, RtwConfigBbGain, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    RtwBbReset(Adapter);

    H2c = RtwAllocate(sizeof(*H2c));
    if (!H2c)
        return STATUS_INSUFFICIENT_RESOURCES;
    for (Path = 0; Path < 2; Path++)
    {
        Status = RtwApplyTable(Adapter, Path == 0 ? RTW_ELEMENT_RADIO_A : RTW_ELEMENT_RADIO_B, Path, RtwConfigRf, H2c);
        if (NT_SUCCESS(Status))
            Status = RtwSendRfRegisters(Adapter, Path, H2c);
        if (!NT_SUCCESS(Status))
            break;
    }
    RtwFree(H2c);
    if (!NT_SUCCESS(Status))
        return Status;

    RtwInitCoex(Adapter);

    RtwBbWrite32(Adapter, R_P0_EN_SOUND_WO_NDP, B_P0_EN_SOUND_WO_NDP, 0);
    RtwBbWrite32(Adapter, R_P1_EN_SOUND_WO_NDP, B_P1_EN_SOUND_WO_NDP, 0);
    for (Address = R_AX_PWR_MACID_LMT_TABLE0; Address <= R_AX_PWR_MACID_LMT_TABLE127; Address += 4)
        RtwWrite32(Adapter, Address, 0);
    Adapter->GainOffsetBase = (CHAR)RtwBbRead32(Adapter, R_P0_RPL1, B_P0_RPL1_BIAS_MASK);
    Adapter->RssiBase = (CHAR)RtwBbRead32(Adapter, R_P1_RPL1, B_P0_RPL1_BIAS_MASK);

    RtwWriteXtalSi(Adapter, XTAL_SI_XTAL_SC_XO, Adapter->XtalCap & RTW_XTAL_SC_MASK, 0xFF);
    RtwWriteXtalSi(Adapter, XTAL_SI_XTAL_SC_XI, Adapter->XtalCap & RTW_XTAL_SC_MASK, 0xFF);

    RtwPowerTrim(Adapter);
    RtwInitRfNctl(Adapter);
    RtwRfkInit(Adapter);

    RtwSetChannel(Adapter, Channel);
    RtwCfgTrxPath(Adapter, Channel);

    DPRINT("RTW8852BS: PHY ready, RFE %u xtal 0x%02x channel %u PD %08lx ADC %08lx RF18 %05lx/%05lx\n",
            Adapter->RfeType, Adapter->XtalCap, Channel,
            RtwBbRead32(Adapter, R_PD_CTRL, 0xFFFFFFFF), RtwBbRead32(Adapter, R_ADC_FIFO, 0xFFFFFFFF),
            RtwRfRead(Adapter, 0, RR_CFGCH, RFREG_MASK), RtwRfRead(Adapter, 1, RR_CFGCH, RFREG_MASK));
    return STATUS_SUCCESS;
}

VOID
RtwSetRxFilter(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN AllBeacons)
{
    if (AllBeacons)
        Adapter->RxFilter &= ~(B_AX_A_BCN_CHK_EN | B_AX_A_BC | B_AX_A_A1_MATCH);
    else
        Adapter->RxFilter |= B_AX_A_BCN_CHK_EN | B_AX_A_BC | B_AX_A_A1_MATCH;
    RtwWrite32(Adapter, R_AX_RX_FLTR_OPT, Adapter->RxFilter);
}
