/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     6 GHz neighbor APs parsed from Reduced Neighbor Report elements
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char UCHAR;
typedef unsigned long ULONG;
typedef UCHAR BOOLEAN;
#define FORCEINLINE static inline
#define _In_
#define _In_reads_bytes_(x)
#define _Out_writes_to_(x, y)

#include "ndis6_rnr.h"

static const UCHAR Bssid1[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const UCHAR Bssid2[6] = { 0x02, 0x66, 0x77, 0x88, 0x99, 0xAA };

static ULONG
AddNeighbor(UCHAR *Out, UCHAR FieldType, UCHAR Count, UCHAR InfoLength, UCHAR Class, UCHAR Channel,
            const UCHAR *const *Bssids)
{
    ULONG Length = 0, Entry;

    Out[Length++] = (UCHAR)(FieldType | ((Count - 1) << 4));
    Out[Length++] = InfoLength;
    Out[Length++] = Class;
    Out[Length++] = Channel;
    for (Entry = 0; Entry < Count; Entry++)
    {
        memset(Out + Length, 0xEE, InfoLength);
        Out[Length] = 0x10;
        if (Bssids != NULL)
            memcpy(Out + Length + 1, Bssids[Entry], 6);
        Length += InfoLength;
    }
    return Length;
}

static ULONG
Element(UCHAR *Out, const UCHAR *Body, ULONG BodyLength)
{
    Out[0] = NDIS6_RNR_ELEMENT_ID;
    Out[1] = (UCHAR)BodyLength;
    memcpy(Out + 2, Body, BodyLength);
    return BodyLength + 2;
}

static void
TestSingle6Ghz(void)
{
    const UCHAR *Bssids[] = { Bssid1 };
    UCHAR Body[64], Ies[80];
    NDIS6_RNR_AP Aps[4];
    ULONG Length = AddNeighbor(Body, 0, 1, 13, 131, 37, Bssids);
    ULONG IesLength = Element(Ies, Body, Length);

    assert(Ndis6RnrCollect6Ghz(Ies, IesLength, Aps, 4) == 1);
    assert(memcmp(Aps[0].Bssid, Bssid1, 6) == 0);
    assert(Aps[0].Channel == 37 && Aps[0].OperatingClass == 131);
}

static void
TestFiltering(void)
{
    const UCHAR *Bssids[] = { Bssid1, Bssid2 };
    UCHAR Body[96], Ies[160];
    NDIS6_RNR_AP Aps[4];
    ULONG Length = 0, IesLength = 0;

    Length += AddNeighbor(Body + Length, 0, 1, 8, 115, 36, Bssids);
    Length += AddNeighbor(Body + Length, 0, 1, 5, 133, 5, NULL);
    Length += AddNeighbor(Body + Length, 1, 1, 8, 131, 21, Bssids);
    Length += AddNeighbor(Body + Length, 0, 2, 7, 134, 69, Bssids);

    Ies[IesLength++] = 0;
    Ies[IesLength++] = 4;
    memcpy(Ies + IesLength, "TEST", 4);
    IesLength += 4;
    IesLength += Element(Ies + IesLength, Body, Length);

    assert(Ndis6RnrCollect6Ghz(Ies, IesLength, Aps, 4) == 2);
    assert(memcmp(Aps[0].Bssid, Bssid1, 6) == 0 && Aps[0].Channel == 69);
    assert(memcmp(Aps[1].Bssid, Bssid2, 6) == 0 && Aps[1].Channel == 69);
    assert(Ndis6RnrCollect6Ghz(Ies, IesLength, Aps, 1) == 1);
}

static void
TestMalformed(void)
{
    const UCHAR *Bssids[] = { Bssid1, Bssid2 };
    UCHAR Body[64], Ies[80];
    NDIS6_RNR_AP Aps[4];
    ULONG Length = AddNeighbor(Body, 0, 2, 13, 131, 37, Bssids);
    ULONG IesLength = Element(Ies, Body, Length);

    Ies[1] = (UCHAR)(Length - 1);
    assert(Ndis6RnrCollect6Ghz(Ies, IesLength - 1, Aps, 4) == 0);
    Ies[1] = (UCHAR)(Length + 10);
    assert(Ndis6RnrCollect6Ghz(Ies, IesLength, Aps, 4) == 0);
    Ies[1] = (UCHAR)Length;
    Body[1] = 0;
    IesLength = Element(Ies, Body, Length);
    assert(Ndis6RnrCollect6Ghz(Ies, IesLength, Aps, 4) == 0);
    assert(Ndis6RnrCollect6Ghz(Ies, 1, Aps, 4) == 0);
}

static void
TestTbttLayouts(void)
{
    static const ULONG With[] = { 7, 8, 9, 11, 12, 13, 16 };
    static const ULONG Without[] = { 1, 2, 5, 6, 10, 14, 15 };
    ULONG i;

    for (i = 0; i < sizeof(With) / sizeof(With[0]); i++)
        assert(Ndis6RnrTbttHasBssid(With[i]));
    for (i = 0; i < sizeof(Without) / sizeof(Without[0]); i++)
        assert(!Ndis6RnrTbttHasBssid(Without[i]));
}

int
main(void)
{
    TestSingle6Ghz();
    TestFiltering();
    TestMalformed();
    TestTbttLayouts();
    printf("ndis.rnr: ok\n");
    return 0;
}
