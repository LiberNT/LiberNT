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

#endif /* _NDIS6_WDI_CAPS_H_ */
