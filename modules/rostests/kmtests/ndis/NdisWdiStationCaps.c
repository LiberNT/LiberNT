/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The WDI station capability that makes the host query the adapter's BSS list cache
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <ndis.h>
#include <windot11.h>
#include "../../../../drivers/network/ndis/include/ndis6_wdi_caps.h"

static
ULONG
BuildStationCaps(
    _Out_writes_bytes_(64) PUCHAR Caps,
    _In_ UCHAR QoS,
    _In_ UCHAR HostFips,
    _In_ UCHAR Mfp,
    _In_ UCHAR AutoPowerSave,
    _In_ UCHAR BssListCache)
{
    ULONG Length = 0;
    ULONG Index;

    RtlZeroMemory(Caps, 64);
    for (Index = 0; Index < 8; Index++)
    {
        ULONG Size = 4 + Index;

        RtlCopyMemory(Caps + Length, &Size, sizeof(Size));
        Length += sizeof(Size);
    }
    Caps[Length++] = QoS;
    Caps[Length++] = HostFips;
    Caps[Length++] = Mfp;
    Caps[Length++] = AutoPowerSave;
    Caps[Length++] = BssListCache;
    Caps[Length++] = 1;
    return Length;
}

START_TEST(NdisWdiStationCaps)
{
    UCHAR Caps[64];
    ULONG Length;

    Length = BuildStationCaps(Caps, 1, 0, 1, 1, 1);
    ok_eq_ulong(Length, 38UL);
    ok_bool_true(Ndis6WdiStationCapsBssListCached(Caps, Length), "an adapter that maintains the BSS list cache is queried for it");

    Length = BuildStationCaps(Caps, 1, 1, 1, 1, 0);
    ok_bool_false(Ndis6WdiStationCapsBssListCached(Caps, Length), "the flags before the cache flag do not select the cache");

    (VOID)BuildStationCaps(Caps, 0, 0, 0, 0, 1);
    ok_bool_false(Ndis6WdiStationCapsBssListCached(Caps, NDIS6_WDI_STATION_CAPS_BSS_LIST_CACHE),
                  "a capability block that ends before the cache flag does not select the cache");
    ok_bool_true(Ndis6WdiStationCapsBssListCached(Caps, NDIS6_WDI_STATION_CAPS_BSS_LIST_CACHE + 1),
                 "the cache flag is the fifth byte after the eight table sizes");
}
