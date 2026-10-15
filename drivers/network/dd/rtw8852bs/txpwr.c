/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Transmit power tables, limits, shaping and trims
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright (c) 2019-2024 Realtek Corporation (BSD-3-Clause, see COPYING.rtw89)
 */

#include "rtw8852bs.h"
#include "phyreg.h"

#define NDEBUG
#include <debug.h>

#define RTW_ELEMENT_TXPWR_BYRATE        9
#define RTW_ELEMENT_TXPWR_LMT_2GHZ      10
#define RTW_ELEMENT_TXPWR_LMT_5GHZ      11
#define RTW_ELEMENT_TXPWR_LMT_RU_2GHZ   13
#define RTW_ELEMENT_TXPWR_LMT_RU_5GHZ   14
#define RTW_ELEMENT_TX_SHAPE_LMT        16

#define RTW_BAND_2G                     0
#define RTW_BAND_5G                     1
#define RTW_RS_CCK                      0
#define RTW_RS_OFDM                     1
#define RTW_RS_MCS                      2
#define RTW_RS_HEDCM                    3
#define RTW_RS_OFFSET                   4
#define RTW_BW_20                       0
#define RTW_BW_40                       1
#define RTW_REGD_WW                     0
#define RTW_RU26                        0
#define RTW_RU52                        1
#define RTW_RU106                       2
#define RTW_BF_NUM                      2
#define RTW_NTX_NUM                     2
#define RTW_RATE_OFFSET_NUM             5
#define RTW_TXPWR_LMT_PAGE_SIZE         40
#define RTW_TXPWR_LMT_RU_PAGE_SIZE      24
#define RTW_TSSI_16DBM_CW               0x12C
#define RTW_BASE_CW_0DB                 0x27

static const UCHAR RtwRateCount[] = { 4, 8, 12, 4 };
static const UCHAR RtwNssCount[] = { 1, 1, 4, 2 };

static const ULONG RtwDfirFlat[8] =
{
    0x023D23FF, 0x0029B354, 0x000FC1C8, 0x00FDB053, 0x00F86F9A, 0x06FAEF92, 0x00FE5FCC, 0x00FFDFF5
};
static const ULONG RtwDfirSharp[8] =
{
    0x023D83FF, 0x002C636A, 0x0013F204, 0x00008090, 0x00F87FB0, 0x06F99F83, 0x00FDBFBA, 0x00003FF5
};
static const ULONG RtwDfirSharp14[8] =
{
    0x023B13FF, 0x001C42DE, 0x00FDB0AD, 0x00F60F6E, 0x00FD8F92, 0x0602D011, 0x0001C02C, 0x00FFF00A
};

static
UCHAR
RtwChannelIndex(
    _In_ UCHAR Channel)
{
    if (Channel >= 1 && Channel <= 14)
        return Channel - 1;
    if (Channel >= 36 && Channel <= 64)
        return (Channel - 36) / 2;
    if (Channel >= 100 && Channel <= 144)
        return (Channel - 100) / 2 + 15;
    if (Channel >= 149 && Channel <= 177)
        return (Channel - 149) / 2 + 38;
    return 0;
}

static
CHAR
RtwRfToMac(
    _In_ CHAR Value)
{
    return (CHAR)(Value >> 1);
}

static
CHAR
RtwReadByRate(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Band,
    _In_ UCHAR Rs,
    _In_ UCHAR Nss,
    _In_ UCHAR Index)
{
    const UCHAR *Table, *Entry;
    ULONG EntrySize, Count, i;
    CHAR Value = 0;

    if (Rs == RTW_RS_CCK)
        Band = RTW_BAND_2G;

    Table = RtwFindTxPowerElement(Adapter, RTW_ELEMENT_TXPWR_BYRATE, &EntrySize, &Count);
    if (!Table || EntrySize < 9)
        return 0;

    for (i = 0; i < Count; i++)
    {
        Entry = Table + i * EntrySize;
        if (Entry[0] != Band || Entry[2] != Rs)
            continue;
        if ((Rs == RTW_RS_MCS || Rs == RTW_RS_HEDCM) && Entry[1] != Nss)
            continue;
        if (Index < Entry[3] || Index >= Entry[3] + Entry[4] || Index - Entry[3] >= 4)
            continue;
        Value = (CHAR)Entry[5 + Index - Entry[3]];
    }

    return RtwRfToMac(Value);
}

static
CHAR
RtwReadLimit(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Band,
    _In_ UCHAR Bandwidth,
    _In_ UCHAR Ntx,
    _In_ UCHAR Rs,
    _In_ UCHAR Bf,
    _In_ UCHAR Channel)
{
    const UCHAR *Table, *Entry;
    ULONG EntrySize, Count, i;
    UCHAR Index = RtwChannelIndex(Channel);
    CHAR Value = 0;

    Table = RtwFindTxPowerElement(Adapter,
                                  Band == RTW_BAND_2G ? RTW_ELEMENT_TXPWR_LMT_2GHZ : RTW_ELEMENT_TXPWR_LMT_5GHZ,
                                  &EntrySize, &Count);
    if (!Table || EntrySize < 7)
        return 0;

    for (i = 0; i < Count; i++)
    {
        Entry = Table + i * EntrySize;
        if (Entry[0] == Bandwidth && Entry[1] == Ntx && Entry[2] == Rs && Entry[3] == Bf &&
            Entry[4] == RTW_REGD_WW && Entry[5] == Index)
        {
            Value = (CHAR)Entry[6];
        }
    }

    return RtwRfToMac(Value);
}

static
CHAR
RtwReadLimitRu(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Band,
    _In_ UCHAR Ru,
    _In_ UCHAR Ntx,
    _In_ UCHAR Channel)
{
    const UCHAR *Table, *Entry;
    ULONG EntrySize, Count, i;
    UCHAR Index = RtwChannelIndex(Channel);
    CHAR Value = 0;

    Table = RtwFindTxPowerElement(Adapter,
                                  Band == RTW_BAND_2G ? RTW_ELEMENT_TXPWR_LMT_RU_2GHZ : RTW_ELEMENT_TXPWR_LMT_RU_5GHZ,
                                  &EntrySize, &Count);
    if (!Table || EntrySize < 5)
        return 0;

    for (i = 0; i < Count; i++)
    {
        Entry = Table + i * EntrySize;
        if (Entry[0] == Ru && Entry[1] == Ntx && Entry[2] == RTW_REGD_WW && Entry[3] == Index)
            Value = (CHAR)Entry[4];
    }

    return RtwRfToMac(Value);
}

static
UCHAR
RtwReadTxShape(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Band,
    _In_ UCHAR Rs)
{
    const UCHAR *Table, *Entry;
    ULONG EntrySize, Count, i;
    UCHAR Value = 0;

    Table = RtwFindTxPowerElement(Adapter, RTW_ELEMENT_TX_SHAPE_LMT, &EntrySize, &Count);
    if (!Table || EntrySize < 4)
        return 0;

    for (i = 0; i < Count; i++)
    {
        Entry = Table + i * EntrySize;
        if (Entry[0] == Band && Entry[1] == Rs && Entry[2] == RTW_REGD_WW)
            Value = Entry[3];
    }

    return Value;
}

static
ULONG
RtwPack4(
    _In_reads_(4) const CHAR *Values)
{
    return (UCHAR)Values[0] | ((ULONG)(UCHAR)Values[1] << 8) |
           ((ULONG)(UCHAR)Values[2] << 16) | ((ULONG)(UCHAR)Values[3] << 24);
}

static
VOID
RtwSetByRate(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Band)
{
    static const UCHAR Sections[] = { RTW_RS_CCK, RTW_RS_OFDM, RTW_RS_MCS, RTW_RS_HEDCM };
    ULONG Address = R_AX_PWR_BY_RATE_TABLE0, i;
    UCHAR Nss, Index;
    CHAR Values[4];

    for (Nss = 0; Nss < RTW_NTX_NUM; Nss++)
    {
        for (i = 0; i < RTL_NUMBER_OF(Sections); i++)
        {
            if (Nss >= RtwNssCount[Sections[i]])
                continue;

            for (Index = 0; Index < RtwRateCount[Sections[i]]; Index++)
            {
                Values[Index % 4] = RtwReadByRate(Adapter, Band, Sections[i], Nss, Index);
                if ((Index + 1) % 4)
                    continue;
                RtwWrite32(Adapter, Address, RtwPack4(Values));
                Address += 4;
            }
        }
    }
}

static
VOID
RtwSetOffset(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Band)
{
    ULONG Value = 0;
    UCHAR Index;

    for (Index = 0; Index < RTW_RATE_OFFSET_NUM; Index++)
        Value |= ((ULONG)RtwReadByRate(Adapter, Band, RTW_RS_OFFSET, 0, Index) & 0xF) << (Index * 4);

    RtwWrite32Mask(Adapter, R_AX_PWR_RATE_OFST_CTRL, 0x000FFFFF, Value);
}

static
VOID
RtwSetTxShape(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Band,
    _In_ UCHAR Channel)
{
    const ULONG *Dfir;
    ULONG i;

    if (Band == RTW_BAND_2G)
    {
        if (Channel == 14)
            Dfir = RtwDfirSharp14;
        else
            Dfir = RtwReadTxShape(Adapter, Band, RTW_RS_CCK) == 0 ? RtwDfirFlat : RtwDfirSharp;
        for (i = 0; i < 8; i++)
            RtwBbWrite32(Adapter, R_TXFIR0 + i * 4, 0xFFFFFFFF, Dfir[i]);
    }

    RtwBbWrite32(Adapter, R_DCFO_OPT, B_TXSHAPE_TRIANGULAR_CFG, RtwReadTxShape(Adapter, Band, RTW_RS_OFDM));
}

static
VOID
RtwSetLimit(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Band,
    _In_ UCHAR Channel)
{
    CHAR Page[RTW_TXPWR_LMT_PAGE_SIZE];
    ULONG Address = R_AX_PWR_LMT_TABLE0, i;
    UCHAR Ntx, Bf;

    for (Ntx = 0; Ntx < RTW_NTX_NUM; Ntx++)
    {
        RtlZeroMemory(Page, sizeof(Page));
        for (Bf = 0; Bf < RTW_BF_NUM; Bf++)
        {
            Page[0 + Bf] = RtwReadLimit(Adapter, Band, RTW_BW_20, Ntx, RTW_RS_CCK, Bf, Channel);
            Page[2 + Bf] = RtwReadLimit(Adapter, Band, RTW_BW_40, Ntx, RTW_RS_CCK, Bf, Channel);
            Page[4 + Bf] = RtwReadLimit(Adapter, Band, RTW_BW_20, Ntx, RTW_RS_OFDM, Bf, Channel);
            Page[6 + Bf] = RtwReadLimit(Adapter, Band, RTW_BW_20, Ntx, RTW_RS_MCS, Bf, Channel);
        }
        for (i = 0; i < sizeof(Page); i += 4, Address += 4)
            RtwWrite32(Adapter, Address, RtwPack4(Page + i));
    }
}

static
VOID
RtwSetLimitRu(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Band,
    _In_ UCHAR Channel)
{
    CHAR Page[RTW_TXPWR_LMT_RU_PAGE_SIZE];
    ULONG Address = R_AX_PWR_RU_LMT_TABLE0, i;
    UCHAR Ntx;

    for (Ntx = 0; Ntx < RTW_NTX_NUM; Ntx++)
    {
        RtlZeroMemory(Page, sizeof(Page));
        Page[0] = RtwReadLimitRu(Adapter, Band, RTW_RU26, Ntx, Channel);
        Page[8] = RtwReadLimitRu(Adapter, Band, RTW_RU52, Ntx, Channel);
        Page[16] = RtwReadLimitRu(Adapter, Band, RTW_RU106, Ntx, Channel);
        for (i = 0; i < sizeof(Page); i += 4, Address += 4)
            RtwWrite32(Adapter, Address, RtwPack4(Page + i));
    }
}

static
ULONG
RtwTxPowerReference(
    _In_ SHORT Reference)
{
    SHORT Power = (SHORT)((Reference << 1) + (RTW_BASE_CW_0DB << 3));
    ULONG BbPower = Power & 0x7;
    LONG RfPower = (Power >> 3) & 0x3F;
    ULONG TssiOffset = (ULONG)(RTW_TSSI_16DBM_CW + (Reference << 1) - (16 << 3));

    RfPower = max(15, min(63, RfPower));
    return ((TssiOffset << 18) & B_DPD_TSSI_CW) |
           ((((ULONG)RfPower << 3) | BbPower) << 9 & B_DPD_PWR_CW) |
           ((ULONG)Reference & B_DPD_REF);
}

static
VOID
RtwSetTxPowerReference(
    _In_ PRTW_ADAPTER Adapter)
{
    static const ULONG PathBase[2] = { 0x5800, 0x7800 };
    const ULONG Mask = B_DPD_TSSI_CW | B_DPD_PWR_CW | B_DPD_REF;
    ULONG Value = RtwTxPowerReference(0);
    ULONG Path;

    RtwWrite32Mask(Adapter, R_AX_PWR_RATE_CTRL, B_AX_PWR_REF, 0);
    for (Path = 0; Path < 2; Path++)
    {
        RtwBbWrite32(Adapter, PathBase[Path] + 4, Mask, Value);
        RtwBbWrite32(Adapter, PathBase[Path] + 8, Mask, Value);
    }
}

VOID
RtwSetTxPower(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel)
{
    UCHAR Band = Channel <= 14 ? RTW_BAND_2G : RTW_BAND_5G;

    RtwSetByRate(Adapter, Band);
    RtwSetOffset(Adapter, Band);
    RtwSetTxShape(Adapter, Band, Channel);
    RtwSetLimit(Adapter, Band, Channel);
    RtwSetLimitRu(Adapter, Band, Channel);
    RtwSetTxPowerReference(Adapter);
}

VOID
RtwPowerTrim(
    _In_ PRTW_ADAPTER Adapter)
{
    static const ULONG ThermalTrim[2] = { 0x5DF, 0x5DC };
    static const ULONG PaBiasTrim[2] = { 0x5DE, 0x5DB };
    UCHAR Thermal[2], PaBias[2];
    ULONG Path;

    for (Path = 0; Path < 2; Path++)
    {
        Thermal[Path] = Adapter->PhyCap[ThermalTrim[Path] - RTW_EFUSE_PHYCAP_OFFSET];
        PaBias[Path] = Adapter->PhyCap[PaBiasTrim[Path] - RTW_EFUSE_PHYCAP_OFFSET];
    }

    if (Thermal[0] != 0xFF || Thermal[1] != 0xFF)
    {
        for (Path = 0; Path < 2; Path++)
            RtwRfWrite(Adapter, (UCHAR)Path, RR_TM2, RR_TM2_OFF,
                       ((Thermal[Path] & 0x1) << 3) | ((Thermal[Path] & 0x1F) >> 1));
    }

    if (PaBias[0] != 0xFF || PaBias[1] != 0xFF)
    {
        for (Path = 0; Path < 2; Path++)
        {
            RtwRfWrite(Adapter, (UCHAR)Path, RR_BIASA, RR_BIASA_TXG, PaBias[Path] & 0xF);
            RtwRfWrite(Adapter, (UCHAR)Path, RR_BIASA, RR_BIASA_TXA, PaBias[Path] >> 4);
        }
    }

    DPRINT("RTW8852BS: power trim thermal %02x/%02x PA bias %02x/%02x\n",
            Thermal[0], Thermal[1], PaBias[0], PaBias[1]);
}
