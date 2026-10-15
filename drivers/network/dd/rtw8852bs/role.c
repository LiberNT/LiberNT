/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Station role, port, address CAM and control table setup
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright (c) 2019-2024 Realtek Corporation (BSD-3-Clause, see COPYING.rtw89)
 */

#include "rtw8852bs.h"

#define NDEBUG
#include <debug.h>

#define RTW_MACID                       0
#define RTW_PORT                        0
#define RTW_NET_TYPE_NO_LINK            0
#define RTW_NET_TYPE_INFRA              2
#define RTW_SELF_ROLE_CLIENT            0
#define RTW_WIFI_ROLE_STATION           1
#define RTW_ROLE_CREATE                 0
#define RTW_ADDR_CAM_SEC_NORMAL         2
#define RTW_ADDR_CAM_ENTRY_SIZE         0x40
#define RTW_BSSID_CAM_ENTRY_SIZE        0x08
#define RTW_BSSID_MATCH_ALL             0x3F
#define RTW_BEACON_INTERVAL_DEFAULT     100
#define RTW_BCN_EARLY_DEFAULT           160
#define RTW_BCN_SETUP_DEFAULT           2
#define RTW_BCN_HOLD_DEFAULT            200
#define RTW_TBTT_EARLY_DEFAULT          5
#define RTW_TBTT_AGG_DEFAULT            1
#define RTW_CMC_TABLE_WORDS             17
#define RTW_CMC_MASK_OFFSET             8
#define RTW_HW_RATE_CCK1                0x00
#define RTW_HW_RATE_OFDM6               0x04

#define RTW_H2C_CL_MAC_FR_EXCHG         5
#define RTW_H2C_FUNC_CCTLINFO_UD        2
#define RTW_H2C_CL_MAC_ADDR_CAM_UPDATE  6
#define RTW_H2C_FUNC_ADDR_CAM_UPD       0
#define RTW_H2C_CL_MAC_MEDIA_RPT        8
#define RTW_H2C_FUNC_JOININFO           0
#define RTW_H2C_FUNC_FWROLE_MAINTAIN    4
#define RTW_H2C_CL_MAC_FW_OFLD          9
#define RTW_H2C_FUNC_MACID_PAUSE        8
#define RTW_H2C_CAT_OUTSRC              2
#define RTW_H2C_CL_OUTSRC_RA            1
#define RTW_H2C_FUNC_OUTSRC_RA_MACIDCFG 0

#define RTW_RA_MODE_CCK                 RTW_BIT(0)
#define RTW_RA_MODE_OFDM                RTW_BIT(1)
#define RTW_RA_MASK_SUBCCK              0x5
#define RTW_H2C_RA_W0_MODE_SHIFT        1
#define RTW_H2C_RA_W0_MACID_SHIFT       8
#define RTW_H2C_RA_W0_UPD_ALL           RTW_BIT(20)

static
ULONG
RtwFieldShift(
    _In_ ULONG Mask)
{
    ULONG Shift = 0;

    while (Shift < 32 && !(Mask & (1UL << Shift)))
        Shift++;
    return Shift;
}

static
VOID
RtwWrite16Mask(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ USHORT Mask,
    _In_ USHORT Value)
{
    USHORT Current = RtwRead16(Adapter, Address);

    RtwWrite16(Adapter, Address, (USHORT)((Current & ~Mask) | ((Value << RtwFieldShift(Mask)) & Mask)));
}

static
VOID
RtwPortUpdate(
    _In_ PRTW_ADAPTER Adapter,
    _In_ USHORT BeaconInterval)
{
    BOOLEAN Infra = Adapter->NetType == RTW_NET_TYPE_INFRA;
    ULONG Value;

    if (RtwRead32(Adapter, R_AX_PORT_CFG_P0) & B_AX_PORT_FUNC_EN)
    {
        RtwSleep(RTW_BEACON_INTERVAL_DEFAULT + 1);
        RtwWrite32Clear(Adapter, R_AX_PORT_CFG_P0, B_AX_PORT_FUNC_EN | B_AX_BRK_SETUP);
        RtwWrite32Set(Adapter, R_AX_PORT_CFG_P0, B_AX_TSFTR_RST);
        RtwWrite32(Adapter, R_AX_BCN_CNT_TMR_P0, 0);
    }

    RtwWrite32Clear(Adapter, R_AX_PORT_CFG_P0, B_AX_TXBCN_RPT_EN | B_AX_RXBCN_RPT_EN);
    RtwWrite32Mask(Adapter, R_AX_PORT_CFG_P0, B_AX_NET_TYPE_MASK, Adapter->NetType);
    if (Adapter->NetType != RTW_NET_TYPE_NO_LINK)
        RtwWrite32Set(Adapter, R_AX_PORT_CFG_P0, B_AX_TBTT_PROHIB_EN | B_AX_BRK_SETUP);
    else
        RtwWrite32Clear(Adapter, R_AX_PORT_CFG_P0, B_AX_TBTT_PROHIB_EN | B_AX_BRK_SETUP);
    if (Infra)
        RtwWrite32Set(Adapter, R_AX_PORT_CFG_P0, B_AX_RX_BSSID_FIT_EN | B_AX_TSF_UDT_EN);
    else
        RtwWrite32Clear(Adapter, R_AX_PORT_CFG_P0, B_AX_RX_BSSID_FIT_EN | B_AX_TSF_UDT_EN);
    RtwWrite32Clear(Adapter, R_AX_PORT_CFG_P0, B_AX_BCNTX_EN);

    RtwWrite32Mask(Adapter, R_AX_BCN_SPACE_CFG_P0, B_AX_BCN_SPACE_MASK,
                   BeaconInterval ? BeaconInterval : RTW_BEACON_INTERVAL_DEFAULT);
    RtwWrite8(Adapter, R_AX_P0MB_HGQ_WINDOW_CFG_0, 0);
    RtwWrite8(Adapter, R_AX_MD_TSFT_STMP_CTL,
              RtwRead8(Adapter, R_AX_MD_TSFT_STMP_CTL) | B_AX_UPD_HGQMD | B_AX_UPD_TIMIE);
    RtwWrite16Mask(Adapter, R_AX_DTIM_CTRL_P0, B_AX_DTIM_NUM_MASK, Infra ? 1 : 0);

    Value = RtwRead32(Adapter, R_AX_MBSSID_DROP_0);
    Value &= ~((1UL << RTW_PORT) << RtwFieldShift(B_AX_PORT_DROP_4_0_MASK));
    Value &= ~RTW_BIT(0);
    RtwWrite32(Adapter, R_AX_MBSSID_DROP_0, Value);

    RtwWrite32Mask(Adapter, R_AX_TBTT_PROHIB_P0, B_AX_TBTT_SETUP_MASK, RTW_BCN_SETUP_DEFAULT);
    RtwWrite32Mask(Adapter, R_AX_TBTT_PROHIB_P0, B_AX_TBTT_HOLD_MASK, RTW_BCN_HOLD_DEFAULT);
    RtwWrite32Mask(Adapter, R_AX_BCN_AREA_P0, B_AX_BCN_MSK_AREA_MASK, 0);
    RtwWrite16Mask(Adapter, R_AX_TBTTERLYINT_CFG_P0, B_AX_TBTTERLY_MASK, RTW_TBTT_EARLY_DEFAULT);
    RtwWrite16Mask(Adapter, R_AX_TBTT_AGG_P0, B_AX_TBTT_AGG_NUM_MASK, RTW_TBTT_AGG_DEFAULT);
    RtwWrite32Mask(Adapter, R_AX_PTCL_BSS_COLOR_0, B_AX_BSS_COLOB_AX_PORT_0_MASK, 0);
    RtwWrite32Clear(Adapter, R_AX_MBSSID_CTRL, B_AX_P0MB_ALL_MASK);
    RtwWrite32Set(Adapter, R_AX_PORT_CFG_P0, B_AX_PORT_FUNC_EN);
    RtwStall(20);
    RtwWrite32Mask(Adapter, R_AX_BCNERLYINT_CFG_P0, B_AX_BCNERLY_MASK, RTW_BCN_EARLY_DEFAULT);
    RtwWrite32Mask(Adapter, R_AX_BCN_PSR_RPT_P0, B_AX_BCAID_P0_MASK, 0);
}

static
VOID
RtwInitMacTables(
    _In_ PRTW_ADAPTER Adapter)
{
    static const ULONG CmacDefaults[8] = { 0x4, 0x400A0004, 0, 0, 0, 0xE43000B, 0, 0xB8109 };
    ULONG i;

    if (Adapter->SecureBoot)
        return;

    for (i = 0; i < 4; i++)
    {
        RtwWrite32(Adapter, R_AX_FILTER_MODEL_ADDR, DMAC_TBL_BASE_ADDR + (RTW_MACID << 4) + (i << 2));
        RtwWrite32(Adapter, R_AX_INDIR_ACCESS_ENTRY, 0);
    }

    RtwWrite32(Adapter, R_AX_FILTER_MODEL_ADDR, CMAC_TBL_BASE_ADDR + RTW_MACID * CCTL_INFO_SIZE);
    for (i = 0; i < RTL_NUMBER_OF(CmacDefaults); i++)
        RtwWrite32(Adapter, R_AX_INDIR_ACCESS_ENTRY + i * 4, CmacDefaults[i]);
}

static
VOID
RtwCmcSet(
    _Inout_updates_(RTW_CMC_TABLE_WORDS) PULONG Table,
    _In_ ULONG Word,
    _In_ ULONG Mask,
    _In_ ULONG Value)
{
    Table[Word] = (Table[Word] & ~Mask) | ((Value << RtwFieldShift(Mask)) & Mask);
    Table[Word + RTW_CMC_MASK_OFFSET] |= Mask;
}

static
NTSTATUS
RtwSendCmcTable(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_(RTW_CMC_TABLE_WORDS) const ULONG *Table)
{
    UCHAR Data[RTW_CMC_TABLE_WORDS * 4];
    ULONG i;

    for (i = 0; i < RTW_CMC_TABLE_WORDS; i++)
        RtwPutLe32(Data + i * 4, Table[i]);

    return RtwSendH2c(Adapter, RTW_H2C_CAT_MAC, RTW_H2C_CL_MAC_FR_EXCHG, RTW_H2C_FUNC_CCTLINFO_UD,
                      TRUE, Data, sizeof(Data));
}

static
NTSTATUS
RtwSendDefaultCmcTable(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Table[RTW_CMC_TABLE_WORDS] = { 0 };

    Table[0] = RTW_MACID | RTW_BIT(7);
    RtwCmcSet(Table, 5, 0x00000E00, 0);
    RtwCmcSet(Table, 6, 0x000F0000, 3);
    RtwCmcSet(Table, 6, 0x00300000, 0);
    RtwCmcSet(Table, 6, 0x00C00000, 1);
    RtwCmcSet(Table, 6, 0x03000000, 0);
    RtwCmcSet(Table, 6, 0x0C000000, 0);
    RtwCmcSet(Table, 6, 0xF0000000, 0);
    RtwCmcSet(Table, 1, RTW_BIT(21), 0);
    RtwCmcSet(Table, 7, 0x000C0000, 0);
    RtwCmcSet(Table, 7, 0x0F000000, 0);
    return RtwSendCmcTable(Adapter, Table);
}

static
NTSTATUS
RtwSendAssocCmcTable(
    _In_ PRTW_ADAPTER Adapter)
{
    ULONG Table[RTW_CMC_TABLE_WORDS] = { 0 };

    Table[0] = RTW_MACID | RTW_BIT(7);
    RtwCmcSet(Table, 1, RTW_BIT(25), 1);
    RtwCmcSet(Table, 1, RTW_BIT(26), 1);
    RtwCmcSet(Table, 2, 0xF0000000, Adapter->CurrentChannel > 14 ? RTW_HW_RATE_OFDM6 : RTW_HW_RATE_CCK1);
    RtwCmcSet(Table, 2, RTW_BIT(11), 0);
    RtwCmcSet(Table, 3, RTW_BIT(6), 0);
    RtwCmcSet(Table, 7, RTW_BIT(17), 1);
    RtwCmcSet(Table, 5, 0x00000007, RTW_PORT);
    RtwCmcSet(Table, 7, 0x00300000, 0);
    return RtwSendCmcTable(Adapter, Table);
}

static
NTSTATUS
RtwSendMacidPause(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Pause)
{
    UCHAR Data[32];

    RtlZeroMemory(Data, sizeof(Data));
    if (Pause)
        RtwPutLe32(Data, RTW_BIT(RTW_MACID));
    RtwPutLe32(Data + 16, RTW_BIT(RTW_MACID));
    return RtwSendH2c(Adapter, RTW_H2C_CAT_MAC, RTW_H2C_CL_MAC_FW_OFLD, RTW_H2C_FUNC_MACID_PAUSE,
                      FALSE, Data, sizeof(Data));
}

static
NTSTATUS
RtwSendRoleMaintain(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG UpdateMode)
{
    UCHAR Data[4];

    RtwPutLe32(Data,
               RTW_MACID |
               ((ULONG)RTW_SELF_ROLE_CLIENT << 8) |
               (UpdateMode << 10) |
               ((ULONG)RTW_WIFI_ROLE_STATION << 13) |
               ((ULONG)RTW_PORT << 19));
    return RtwSendH2c(Adapter, RTW_H2C_CAT_MAC, RTW_H2C_CL_MAC_MEDIA_RPT, RTW_H2C_FUNC_FWROLE_MAINTAIN,
                      TRUE, Data, sizeof(Data));
}

static
NTSTATUS
RtwSendJoinInfo(
    _In_ PRTW_ADAPTER Adapter,
    _In_ BOOLEAN Disconnect)
{
    UCHAR Data[4];

    RtwPutLe32(Data,
               RTW_MACID |
               (Disconnect ? RTW_BIT(8) : 0) |
               ((ULONG)RTW_PORT << 21) |
               ((ULONG)Adapter->NetType << 24) |
               ((ULONG)RTW_WIFI_ROLE_STATION << 26) |
               ((ULONG)RTW_SELF_ROLE_CLIENT << 30));
    return RtwSendH2c(Adapter, RTW_H2C_CAT_MAC, RTW_H2C_CL_MAC_MEDIA_RPT, RTW_H2C_FUNC_JOININFO,
                      TRUE, Data, sizeof(Data));
}

static
UCHAR
RtwAddressHash(
    _In_reads_bytes_(RTW_ADDRESS_LENGTH) const UCHAR *Address)
{
    UCHAR Hash = 0;
    ULONG i;

    for (i = 0; i < RTW_ADDRESS_LENGTH; i++)
        Hash ^= Address[i];
    return Hash;
}

static
NTSTATUS
RtwSendAddressCam(
    _In_ PRTW_ADAPTER Adapter)
{
    const UCHAR *Sma = Adapter->CurrentAddress;
    const UCHAR *Tma = Adapter->Bssid;
    ULONG Words[15] = { 0 };
    UCHAR Data[sizeof(Words)];
    ULONG i;

    Words[1] = 0 | (0 << 8) | ((ULONG)RTW_ADDR_CAM_ENTRY_SIZE << 16);
    Words[2] = RTW_BIT(0) |
               ((ULONG)Adapter->NetType << 1) |
               ((ULONG)RtwAddressHash(Sma) << 16) |
               ((ULONG)RtwAddressHash(Tma) << 24);
    Words[3] = 0;
    Words[4] = Sma[0] | ((ULONG)Sma[1] << 8) | ((ULONG)Sma[2] << 16) | ((ULONG)Sma[3] << 24);
    Words[5] = Sma[4] | ((ULONG)Sma[5] << 8) | ((ULONG)Tma[0] << 16) | ((ULONG)Tma[1] << 24);
    Words[6] = Tma[2] | ((ULONG)Tma[3] << 8) | ((ULONG)Tma[4] << 16) | ((ULONG)Tma[5] << 24);
    Words[8] = RTW_MACID | ((ULONG)RTW_PORT << 8) | ((ULONG)RTW_PORT << 11);
    Words[9] = (Adapter->NetType == RTW_NET_TYPE_INFRA ? (Adapter->Aid & 0xFFF) : 0) |
               ((ULONG)RTW_ADDR_CAM_SEC_NORMAL << 16);
    Words[12] = 0 | (0 << 8) | ((ULONG)RTW_BSSID_CAM_ENTRY_SIZE << 16);
    Words[13] = RTW_BIT(0) |
                ((ULONG)RTW_BSSID_MATCH_ALL << 2) |
                ((ULONG)Tma[0] << 16) |
                ((ULONG)Tma[1] << 24);
    Words[14] = Tma[2] | ((ULONG)Tma[3] << 8) | ((ULONG)Tma[4] << 16) | ((ULONG)Tma[5] << 24);

    for (i = 0; i < RTL_NUMBER_OF(Words); i++)
        RtwPutLe32(Data + i * 4, Words[i]);

    return RtwSendH2c(Adapter, RTW_H2C_CAT_MAC, RTW_H2C_CL_MAC_ADDR_CAM_UPDATE, RTW_H2C_FUNC_ADDR_CAM_UPD,
                      TRUE, Data, sizeof(Data));
}

NTSTATUS
RtwVifInit(
    _In_ PRTW_ADAPTER Adapter)
{
    NTSTATUS Status;

    Adapter->NetType = RTW_NET_TYPE_NO_LINK;
    Adapter->Aid = 0;
    RtlZeroMemory(Adapter->Bssid, sizeof(Adapter->Bssid));

    RtwPortUpdate(Adapter, 0);
    RtwInitMacTables(Adapter);

    Status = RtwSendMacidPause(Adapter, FALSE);
    if (NT_SUCCESS(Status))
        Status = RtwSendRoleMaintain(Adapter, RTW_ROLE_CREATE);
    if (NT_SUCCESS(Status))
        Status = RtwSendJoinInfo(Adapter, TRUE);
    if (NT_SUCCESS(Status))
        Status = RtwSendAddressCam(Adapter);
    if (NT_SUCCESS(Status))
        Status = RtwSendDefaultCmcTable(Adapter);
    if (!NT_SUCCESS(Status))
        DPRINT1("RTW8852BS: station role setup failed 0x%08lx\n", Status);
    return Status;
}

NTSTATUS
RtwVifSetLink(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_opt_(RTW_ADDRESS_LENGTH) const UCHAR *Bssid,
    _In_ BOOLEAN Associated,
    _In_ USHORT Aid,
    _In_ USHORT BeaconInterval)
{
    UCHAR NetType = Associated ? RTW_NET_TYPE_INFRA : RTW_NET_TYPE_NO_LINK;
    BOOLEAN Changed = NetType != Adapter->NetType;
    NTSTATUS Status;

    if (!Associated)
        Adapter->TxRateMask = 0;
    if (Bssid)
        RtlCopyMemory(Adapter->Bssid, Bssid, RTW_ADDRESS_LENGTH);
    else
        RtlZeroMemory(Adapter->Bssid, RTW_ADDRESS_LENGTH);
    Adapter->NetType = NetType;
    Adapter->Aid = Associated ? Aid : 0;

    if (!Changed)
        return RtwSendAddressCam(Adapter);

    RtwPortUpdate(Adapter, BeaconInterval);
    Status = RtwSendAssocCmcTable(Adapter);
    if (NT_SUCCESS(Status))
        Status = RtwSendJoinInfo(Adapter, !Associated);
    if (NT_SUCCESS(Status))
        Status = RtwSendAddressCam(Adapter);
    return Status;
}

NTSTATUS
RtwVifSetRates(
    _In_ PRTW_ADAPTER Adapter,
    _In_ ULONG RateMask)
{
    UCHAR Data[16];
    ULONG Mode = 0;
    ULONG Mask = RateMask;
    UCHAR Lowest = 0;
    NTSTATUS Status;

    Adapter->TxRateMask = 0;
    if (Mask & RTW_RATE_MASK_CCK)
        Mode |= RTW_RA_MODE_CCK;
    if (Mask & RTW_RATE_MASK_OFDM)
    {
        Mode |= RTW_RA_MODE_OFDM;
        Mask &= RTW_RATE_MASK_OFDM | RTW_RA_MASK_SUBCCK;
    }
    if (!Mask)
        return STATUS_NOT_SUPPORTED;

    while (!(RateMask & (1UL << Lowest)))
        Lowest++;

    RtlZeroMemory(Data, sizeof(Data));
    RtwPutLe32(Data,
               (Mode << RTW_H2C_RA_W0_MODE_SHIFT) |
               ((ULONG)RTW_MACID << RTW_H2C_RA_W0_MACID_SHIFT) |
               RTW_H2C_RA_W0_UPD_ALL);
    RtwPutLe32(Data + 4, Mask);
    Status = RtwSendH2c(Adapter, RTW_H2C_CAT_OUTSRC, RTW_H2C_CL_OUTSRC_RA, RTW_H2C_FUNC_OUTSRC_RA_MACIDCFG,
                        FALSE, Data, sizeof(Data));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RTW8852BS: rate adaptation setup failed 0x%08lx\n", Status);
        return Status;
    }

    Adapter->TxLowestRate = Lowest;
    Adapter->TxRateMask = Mask;
    return STATUS_SUCCESS;
}
