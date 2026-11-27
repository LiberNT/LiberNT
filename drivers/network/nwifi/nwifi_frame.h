/*
 * PROJECT:     LiberNT Native WiFi (802.11) stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IEEE 802.11 and 802.3 framing shared by the driver and its unit test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NWIFI_FRAME_H_
#define _NWIFI_FRAME_H_

/* ===========================================================================
 *  IEEE 802.11 framing
 * ===========================================================================
 */
#define IEEE80211_ADDR_LEN          6
#define IEEE80211_ELEMENT_RATES     1
#define IEEE80211_ELEMENT_RSN       48
#define IEEE80211_ELEMENT_EXT_RATES 50
#define IEEE80211_ELEMENT_RSNXE     244
#define IEEE80211_ELEMENT_EXTENSION 255
#define IEEE80211_EXT_ANTI_CLOGGING_TOKEN 93
#define IEEE80211_RSNXE_SAE_H2E     0x20
#define IEEE80211_SELECTOR_SAE_H2E_ONLY 123
#define IEEE80211_STATUS_SAE_ANTI_CLOGGING 76
#define IEEE80211_STATUS_SAE_UNSUPPORTED_GROUP 77
#define IEEE80211_STATUS_SAE_HASH_TO_ELEMENT 126

/* Frame Control field (little-endian on the air). */
#define IEEE80211_FC0_VERSION_0     0x00
#define IEEE80211_FC0_TYPE_MASK     0x0C
#define IEEE80211_FC0_TYPE_DATA     0x08
#define IEEE80211_FC0_SUBTYPE_MASK  0xF0
#define IEEE80211_FC0_SUBTYPE_DATA  0x00
#define IEEE80211_FC0_SUBTYPE_QOS   0x80

#define IEEE80211_FC1_DIR_MASK      0x03
#define IEEE80211_FC1_DIR_NODS      0x00    /* STA <-> STA (IBSS) */
#define IEEE80211_FC1_DIR_TODS      0x01    /* STA  -> AP  (infra uplink) */
#define IEEE80211_FC1_DIR_FROMDS    0x02    /* AP   -> STA (infra downlink) */
#define IEEE80211_FC1_DIR_DSTODS    0x03    /* WDS */
#define IEEE80211_FC1_PROTECTED     0x40
#define IEEE80211_FC1_ORDER         0x80
#define IEEE80211_QOS_CONTROL_LEN   2
#define IEEE80211_HT_CONTROL_LEN    4
#define IEEE80211_ASSOC_REQ_FIXED_LEN   4
#define IEEE80211_REASSOC_REQ_FIXED_LEN 10

/* 3-address 802.11 MAC header; Addr1/2/3 are interpreted per the ToDS/FromDS
 * bits.  Only the non-QoS 3-address data form is generated/parsed. */
#pragma pack(push, 1)
typedef struct _IEEE80211_MAC_HEADER
{
    UCHAR  FrameControl[2];
    UCHAR  DurationId[2];
    UCHAR  Address1[IEEE80211_ADDR_LEN];
    UCHAR  Address2[IEEE80211_ADDR_LEN];
    UCHAR  Address3[IEEE80211_ADDR_LEN];
    UCHAR  SequenceControl[2];
} IEEE80211_MAC_HEADER, *PIEEE80211_MAC_HEADER;

/* RFC 1042 / 802.2 SNAP header that replaces the Ethernet Type field when an
 * Ethernet II frame is carried as an 802.11 MSDU. */
typedef struct _RFC1042_SNAP_HEADER
{
    UCHAR  Dsap;        /* 0xAA */
    UCHAR  Ssap;        /* 0xAA */
    UCHAR  Control;     /* 0x03 */
    UCHAR  Oui[3];      /* 0x00 0x00 0x00 (or 0x00-00-F8 for bridge tunnel) */
    UCHAR  EtherType[2];
} RFC1042_SNAP_HEADER, *PRFC1042_SNAP_HEADER;
#pragma pack(pop)

#define IEEE80211_MAC_HEADER_LEN    sizeof(IEEE80211_MAC_HEADER)
#define RFC1042_SNAP_HEADER_LEN     sizeof(RFC1042_SNAP_HEADER)

/* 802.3 framing. */
#define ETH_ADDR_LEN                6
#define ETH_HEADER_LEN              14      /* dst(6)+src(6)+type(2) */
#define ETH_TYPE_OFFSET             12
#define ETH_MTU                     1500
#define ETH_MAX_FRAME               (ETH_HEADER_LEN + ETH_MTU)  /* 1514 */

#define NWIFI_MAX_80211_RX_FRAME \
    (IEEE80211_MAC_HEADER_LEN + IEEE80211_QOS_CONTROL_LEN + IEEE80211_HT_CONTROL_LEN + \
     RFC1042_SNAP_HEADER_LEN + ETH_MTU)

FORCEINLINE
ULONG
NwifiDataHeaderLength(
    _In_ UCHAR FrameControl0,
    _In_ UCHAR FrameControl1)
{
    if ((FrameControl0 & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_DATA)
        return 0;

    switch (FrameControl0 & IEEE80211_FC0_SUBTYPE_MASK)
    {
        case IEEE80211_FC0_SUBTYPE_DATA:
            return IEEE80211_MAC_HEADER_LEN;

        case IEEE80211_FC0_SUBTYPE_QOS:
            return IEEE80211_MAC_HEADER_LEN + IEEE80211_QOS_CONTROL_LEN +
                   ((FrameControl1 & IEEE80211_FC1_ORDER) ? IEEE80211_HT_CONTROL_LEN : 0);

        default:
            return 0;
    }
}

#endif
