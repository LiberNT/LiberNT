/*
 * PROJECT: LiberNT API tests
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Pin RTL8168 multicast CRC and station-address checks
 * COPYRIGHT: Copyright 2026 jewchud <kz2hb7hpvp@privaterelay.appleid.com>
 */

#include <apitest.h>

#define ETH_LENGTH_OF_ADDRESS 6

static ULONG
Rtl8168EtherCrc32(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length)
{
    ULONG Crc = 0xffffffff;
    ULONG i;
    ULONG Bit;

    for (i = 0; i < Length; i++)
    {
        UCHAR Data = Buffer[i];
        for (Bit = 0; Bit < 8; Bit++)
        {
            if ((Crc ^ Data) & 1)
                Crc = (Crc >> 1) ^ 0xedb88320;
            else
                Crc >>= 1;
            Data >>= 1;
        }
    }

    return ~Crc;
}

static BOOLEAN
Rtl8168IsValidMacAddress(
    _In_reads_(ETH_LENGTH_OF_ADDRESS) const UCHAR *MacAddress)
{
    ULONG i;
    BOOLEAN AllZero = TRUE;
    BOOLEAN AllOnes = TRUE;

    for (i = 0; i < ETH_LENGTH_OF_ADDRESS; i++)
    {
        if (MacAddress[i] != 0x00)
            AllZero = FALSE;
        if (MacAddress[i] != 0xff)
            AllOnes = FALSE;
    }

    return !AllZero && !AllOnes && !(MacAddress[0] & 0x01);
}

static ULONG
Rtl8168ByteSwapUlong(
    _In_ ULONG Value)
{
    return ((Value & 0x000000ffUL) << 24) |
           ((Value & 0x0000ff00UL) << 8) |
           ((Value & 0x00ff0000UL) >> 8) |
           ((Value & 0xff000000UL) >> 24);
}

static VOID
Rtl8168PcieHashSwap(
    _Inout_updates_(2) ULONG *Hash)
{
    ULONG Tmp = Rtl8168ByteSwapUlong(Hash[0]);
    Hash[0] = Rtl8168ByteSwapUlong(Hash[1]);
    Hash[1] = Tmp;
}

START_TEST(EtherCrc32)
{
    static const UCHAR MulticastIp[] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0x01 };
    static const UCHAR Broadcast[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    static const UCHAR Unicast[] = { 0x02, 0x00, 0x00, 0x81, 0x68, 0x01 };
    static const UCHAR MulticastOui[] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x01 };
    static const UCHAR Zero[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    ULONG Crc;
    ULONG Bit;
    ULONG Hash[2];

    Crc = Rtl8168EtherCrc32(MulticastIp, ETH_LENGTH_OF_ADDRESS);
    ok_hex(Crc, 0x264b3a01);
    Bit = (Crc >> 26) & 0x3f;
    ok_eq_ulong(Bit, 9UL);

    Crc = Rtl8168EtherCrc32(Broadcast, ETH_LENGTH_OF_ADDRESS);
    ok_hex(Crc, 0x41d9ed00);
    Bit = (Crc >> 26) & 0x3f;
    ok_eq_ulong(Bit, 16UL);

    ok_hex(Rtl8168EtherCrc32(Unicast, 0), 0x00000000);

    Hash[0] = 0x00000200;
    Hash[1] = 0x00000000;
    Rtl8168PcieHashSwap(Hash);
    ok_hex(Hash[0], 0x00000000);
    ok_hex(Hash[1], 0x00020000);

    ok(Rtl8168IsValidMacAddress(Unicast), "unicast LAA should be valid\n");
    ok(!Rtl8168IsValidMacAddress(Zero), "all-zero MAC invalid\n");
    ok(!Rtl8168IsValidMacAddress(Broadcast), "broadcast MAC invalid\n");
    ok(!Rtl8168IsValidMacAddress(MulticastOui), "multicast MAC invalid\n");
}
