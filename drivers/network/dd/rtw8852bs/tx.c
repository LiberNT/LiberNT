/*
 * PROJECT:     LiberNT Realtek RTL8852BS Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     802.11 frame transmission over the SDIO data channels
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *              Copyright (c) 2019-2024 Realtek Corporation (BSD-3-Clause, see COPYING.rtw89)
 */

#include "rtw8852bs.h"
#include <rsna_ccmp.h>

#define NDEBUG
#include <debug.h>

#define RTW_WD_INFO_LENGTH              24
#define RTW_WD_LENGTH                   (RTW_WD_BODY_LENGTH + RTW_WD_INFO_LENGTH)

#define RTW_WD_BODY0_WD_INFO_EN         RTW_BIT(22)
#define RTW_WD_BODY0_CHANNEL_DMA_SHIFT  16
#define RTW_WD_BODY0_STF_MODE           RTW_BIT(10)
#define RTW_WD_BODY0_HW_SSN_SEL_SHIFT   2
#define RTW_WD_BODY0_HW_SSN_MODE_SHIFT  0
#define RTW_WD_BODY2_MACID_SHIFT        24
#define RTW_WD_BODY2_QSEL_SHIFT         17
#define RTW_WD_BODY2_TXPKT_SIZE_MASK    0x3FFF
#define RTW_WD_INFO0_USE_RATE           RTW_BIT(30)
#define RTW_WD_INFO0_DATA_RATE_SHIFT    16
#define RTW_WD_INFO0_DISDATAFB          RTW_BIT(10)
#define RTW_WD_INFO1_RTY_LOWEST_SHIFT   16
#define RTW_WD_INFO4_HW_RTS_EN          RTW_BIT(31)
#define RTW_WD_INFO4_RTS_EN             RTW_BIT(27)

#define RTW_TXCH_ACH0                   0
#define RTW_TXCH_B0MG                   8
#define RTW_TX_QSEL_BE_0                0x00
#define RTW_TX_QSEL_B0_MGMT             0x12
#define RTW_MGMT_HW_SSN_SEL             1
#define RTW_MGMT_HW_SEQ_MODE            1

#define RTW_HW_RATE_CCK1                0x00
#define RTW_HW_RATE_OFDM6               0x04
#define RTW_HW_RATE_OFDM24              0x08

#define RTW_PLE_PAGE_SHIFT              7
#define RTW_PLE_RESERVED_SPACE          32
#define RTW_PAYLOAD_DESC_SIZE           24
#define RTW_TX_CHANNEL_MAX_PAGES        102
#define RTW_TX_POLLS                    2000

static
ULONG
RtwTxUsedPages(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel)
{
    ULONG Value = RtwRead32(Adapter, R_AX_SDIO_TXPG_0 + (Channel / 2) * 4);

    return (Channel & 1) ? (Value >> 16) & B_AX_SDIO_AVAL_PG_MASK : Value & B_AX_SDIO_AVAL_PG_MASK;
}

static
NTSTATUS
RtwTxWaitPages(
    _In_ PRTW_ADAPTER Adapter,
    _In_ UCHAR Channel,
    _In_ ULONG Length)
{
    ULONG PlePages, Public, Used, Poll;

    PlePages = ((Length + RTW_PLE_RESERVED_SPACE + RTW_PAYLOAD_DESC_SIZE + (1 << RTW_PLE_PAGE_SHIFT) - 1) >>
                RTW_PLE_PAGE_SHIFT) * 2;

    for (Poll = 0; Poll < RTW_TX_POLLS; Poll++)
    {
        if (Adapter->IoError)
            return STATUS_DEVICE_NOT_CONNECTED;

        Public = RtwRead32(Adapter, R_AX_SDIO_TXPG_WP) & B_AX_SDIO_AVAL_PG_MASK;
        Used = RtwTxUsedPages(Adapter, Channel);
        if (Used < RTW_TX_CHANNEL_MAX_PAGES && Public >= PlePages)
            return STATUS_SUCCESS;

        KeStallExecutionProcessor(50);
    }

    DPRINT1("RTW8852BS: TX channel %u full, used %lu public %lu need %lu\n", Channel, Used, Public, PlePages);
    return STATUS_DEVICE_BUSY;
}

NTSTATUS
RtwTxFrame(
    _In_ PRTW_ADAPTER Adapter,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length,
    _In_ BOOLEAN Management)
{
    PUCHAR Wd = Adapter->TxBuffer;
    UCHAR Channel, Qsel, Rate, LowestRate;
    BOOLEAN Group;
    NTSTATUS Status;

    if (Length < 24 || Length + RTW_WD_LENGTH > RTW_TX_BUFFER_SIZE || !Adapter->PowerOn)
        return STATUS_INVALID_PARAMETER;

    Group = (Frame[4] & 0x01) != 0;
    LowestRate = Adapter->CurrentChannel > 14 ? RTW_HW_RATE_OFDM6 : RTW_HW_RATE_CCK1;
    if (Management || Group)
        Rate = LowestRate;
    else
        Rate = RTW_HW_RATE_OFDM24;
    Channel = Management ? RTW_TXCH_B0MG : RTW_TXCH_ACH0;
    Qsel = Management ? RTW_TX_QSEL_B0_MGMT : RTW_TX_QSEL_BE_0;

    ExAcquireFastMutex(&Adapter->TxLock);

    RtlZeroMemory(Wd, RTW_WD_LENGTH);
    RtwPutLe32(Wd,
               RTW_WD_BODY0_WD_INFO_EN |
               ((ULONG)Channel << RTW_WD_BODY0_CHANNEL_DMA_SHIFT) |
               RTW_WD_BODY0_STF_MODE |
               (RTW_MGMT_HW_SSN_SEL << RTW_WD_BODY0_HW_SSN_SEL_SHIFT) |
               (RTW_MGMT_HW_SEQ_MODE << RTW_WD_BODY0_HW_SSN_MODE_SHIFT));
    RtwPutLe32(Wd + 8,
               (Length & RTW_WD_BODY2_TXPKT_SIZE_MASK) |
               ((ULONG)Qsel << RTW_WD_BODY2_QSEL_SHIFT) |
               (0UL << RTW_WD_BODY2_MACID_SHIFT));
    RtwPutLe32(Wd + RTW_WD_BODY_LENGTH,
               RTW_WD_INFO0_USE_RATE |
               ((ULONG)Rate << RTW_WD_INFO0_DATA_RATE_SHIFT) |
               (Management ? RTW_WD_INFO0_DISDATAFB : 0));
    RtwPutLe32(Wd + RTW_WD_BODY_LENGTH + 4, (ULONG)LowestRate << RTW_WD_INFO1_RTY_LOWEST_SHIFT);
    RtwPutLe32(Wd + RTW_WD_BODY_LENGTH + 16, (Group ? 0 : RTW_WD_INFO4_RTS_EN) | RTW_WD_INFO4_HW_RTS_EN);
    RtlCopyMemory(Wd + RTW_WD_LENGTH, Frame, Length);

    Status = RtwTxWaitPages(Adapter, Channel, RTW_WD_LENGTH + Length);
    if (NT_SUCCESS(Status))
        Status = RtwSdioWritePacket(Adapter, Channel, RTW_WD_LENGTH + Length);

    ExReleaseFastMutex(&Adapter->TxLock);
    return Status;
}

static
VOID
RtwSoftwareEncrypt(
    _In_ PRTW_ADAPTER Adapter,
    _Inout_updates_bytes_(*Length + RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN) PUCHAR Frame,
    _Inout_ PULONG Length)
{
    ULONG HeaderLength, BodyLength;
    ULONGLONG Pn = 0;
    UCHAR Tk[16];
    BOOLEAN Valid;

    if ((Frame[0] & DOT11_FC0_TYPE_MASK) != DOT11_FC0_TYPE_DATA || (Frame[4] & 0x01))
        return;

    NdisAcquireSpinLock(&Adapter->Lock);
    Valid = Adapter->SoftwareCrypto && Adapter->MgmtKeyValid;
    if (Valid)
    {
        Pn = ++Adapter->MgmtTxPn;
        RtlCopyMemory(Tk, Adapter->MgmtTk, sizeof(Tk));
    }
    NdisReleaseSpinLock(&Adapter->Lock);
    if (!Valid)
        return;

    HeaderLength = RtwFrameHeaderLength(Frame, *Length);
    BodyLength = *Length - HeaderLength;
    RtlMoveMemory(Frame + HeaderLength + RSNA_CCMP_HDR_LEN, Frame + HeaderLength, BodyLength);
    Frame[1] |= DOT11_FC1_PROTECTED;
    RsnaCcmpEncrypt(Tk, Frame, HeaderLength, Pn, 0,
                    Frame + HeaderLength + RSNA_CCMP_HDR_LEN, BodyLength,
                    Frame + HeaderLength);
    RtlSecureZeroMemory(Tk, sizeof(Tk));
    *Length += RSNA_CCMP_HDR_LEN + RSNA_CCMP_MIC_LEN;
}

NTSTATUS
RtwSendNetBuffer(
    _In_ PRTW_ADAPTER Adapter,
    _In_ PNET_BUFFER NetBuffer,
    _In_ BOOLEAN Exempt)
{
    ULONG Length = NET_BUFFER_DATA_LENGTH(NetBuffer);
    PUCHAR Storage = Adapter->TxFrame, Data;

    if (Length < DOT11_HEADER_LENGTH || Length > RTW_MAX_80211_FRAME)
        return STATUS_INVALID_PARAMETER;

    Data = NdisGetDataBuffer(NetBuffer, Length, Storage, 1, 0);
    if (!Data)
        return STATUS_INVALID_PARAMETER;
    if (Data != Storage)
        RtlCopyMemory(Storage, Data, Length);

    if (!Exempt)
        RtwSoftwareEncrypt(Adapter, Storage, &Length);

    Adapter->TxBytes += Length;
    return RtwTxFrame(Adapter, Storage, Length, FALSE);
}
