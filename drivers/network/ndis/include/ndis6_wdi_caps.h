/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WDI station capability fields read by the host
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NDIS6_WDI_CAPS_H_
#define _NDIS6_WDI_CAPS_H_

#define NDIS6_WDI_STATION_CAPS_BSS_LIST_CACHE 36

FORCEINLINE
BOOLEAN
Ndis6WdiStationCapsBssListCached(
    _In_reads_bytes_(Length) const UCHAR *Value,
    _In_ ULONG Length)
{
    return Length > NDIS6_WDI_STATION_CAPS_BSS_LIST_CACHE &&
           Value[NDIS6_WDI_STATION_CAPS_BSS_LIST_CACHE] != 0;
}

#define NDIS6_WDI_STATION_CAPS_LIST_SIZES 8

FORCEINLINE
BOOLEAN
Ndis6WdiStationCapsExtSta(
    _In_reads_bytes_(Length) const UCHAR *Value,
    _In_ ULONG Length,
    _Out_ PDOT11_EXTSTA_CAPABILITY Capability)
{
    ULONG Sizes[NDIS6_WDI_STATION_CAPS_LIST_SIZES];

    RtlZeroMemory(Capability, sizeof(*Capability));
    if (Length < sizeof(Sizes))
        return FALSE;
    RtlCopyMemory(Sizes, Value, sizeof(Sizes));

    Capability->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Capability->Header.Revision = DOT11_EXTSTA_CAPABILITY_REVISION_1;
    Capability->Header.Size = sizeof(DOT11_EXTSTA_CAPABILITY);
    Capability->uScanSSIDListSize = Sizes[0];
    Capability->uDesiredBSSIDListSize = Sizes[1];
    Capability->uDesiredSSIDListSize = Sizes[2];
    Capability->uPrivacyExemptionListSize = Sizes[3];
    Capability->uKeyMappingTableSize = Sizes[4];
    Capability->uDefaultKeyTableSize = Sizes[5];
    Capability->uWEPKeyValueMaxLength = Sizes[6];
    Capability->uMaxNumPerSTADefaultKeyTables = Sizes[7];
    /* The host keeps no excluded MAC address list and no PMKID cache, so both stay 0. */
    return TRUE;
}

#endif /* _NDIS6_WDI_CAPS_H_ */
