/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the 802.11 data header lengths and the receive frame capacity of nwifi
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <nwifi_frame.h>

static
VOID
TestDataHeaderLength(VOID)
{
    ok_long(NwifiDataHeaderLength(IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_DATA, 0), 24);
    ok_long(NwifiDataHeaderLength(IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_DATA, IEEE80211_FC1_DIR_FROMDS), 24);
    ok_long(NwifiDataHeaderLength(IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_QOS, 0), 26);
    ok_long(NwifiDataHeaderLength(IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_QOS,
                                  IEEE80211_FC1_DIR_FROMDS | IEEE80211_FC1_PROTECTED), 26);
    ok_long(NwifiDataHeaderLength(IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_QOS,
                                  IEEE80211_FC1_DIR_FROMDS | IEEE80211_FC1_ORDER), 30);

    ok_long(NwifiDataHeaderLength(0x00, 0), 0);
    ok_long(NwifiDataHeaderLength(0x04, 0), 0);
    ok_long(NwifiDataHeaderLength(IEEE80211_FC0_TYPE_DATA | 0x40, 0), 0);
    ok_long(NwifiDataHeaderLength(IEEE80211_FC0_TYPE_DATA | 0xC0, 0), 0);
}

static
VOID
TestReceiveCapacity(VOID)
{
    static const UCHAR FrameControl1[] = { 0, IEEE80211_FC1_DIR_FROMDS, IEEE80211_FC1_DIR_FROMDS | IEEE80211_FC1_ORDER };
    static const UCHAR FrameControl0[] =
    {
        IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_DATA,
        IEEE80211_FC0_TYPE_DATA | IEEE80211_FC0_SUBTYPE_QOS
    };
    ULONG Index0, Index1;

    ok_long(NWIFI_MAX_80211_RX_FRAME, 1538);

    for (Index0 = 0; Index0 < ARRAYSIZE(FrameControl0); Index0++)
    {
        for (Index1 = 0; Index1 < ARRAYSIZE(FrameControl1); Index1++)
        {
            ULONG Length = NwifiDataHeaderLength(FrameControl0[Index0], FrameControl1[Index1]) +
                           RFC1042_SNAP_HEADER_LEN + ETH_MTU;

            ok(Length <= NWIFI_MAX_80211_RX_FRAME,
               "frame control %02x %02x: a %u-byte MSDU needs %lu bytes, the receive buffer holds %u\n",
               FrameControl0[Index0], FrameControl1[Index1], ETH_MTU, Length, (unsigned)NWIFI_MAX_80211_RX_FRAME);
        }
    }
}

START_TEST(RxFrame)
{
    TestDataHeaderLength();
    TestReceiveCapacity();
}
