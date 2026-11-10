/*
 * PROJECT:     LiberNT NDIS library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     6 GHz neighbor APs from IEEE 802.11ax Reduced Neighbor Report elements
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#ifndef _NDIS6_RNR_H_
#define _NDIS6_RNR_H_

#define NDIS6_RNR_ELEMENT_ID        201
#define NDIS6_RNR_6GHZ_CLASS_FIRST  131
#define NDIS6_RNR_6GHZ_CLASS_LAST   137

typedef struct _NDIS6_RNR_AP
{
    UCHAR Bssid[6];
    UCHAR OperatingClass;
    UCHAR Channel;
} NDIS6_RNR_AP, *PNDIS6_RNR_AP;

FORCEINLINE
BOOLEAN
Ndis6RnrTbttHasBssid(
    _In_ ULONG InfoLength)
{
    return (BOOLEAN)(InfoLength == 7 || InfoLength == 8 || InfoLength == 9 ||
                     InfoLength == 11 || InfoLength == 12 || InfoLength == 13 ||
                     InfoLength >= 16);
}

FORCEINLINE
ULONG
Ndis6RnrParseElement(
    _In_reads_bytes_(Length) const UCHAR *Body,
    _In_ ULONG Length,
    _Out_writes_to_(Capacity, return) PNDIS6_RNR_AP Aps,
    _In_ ULONG Capacity)
{
    ULONG Offset = 0;
    ULONG Found = 0;

    while (Offset <= Length && Length - Offset >= 4)
    {
        ULONG Header = Body[Offset] | ((ULONG)Body[Offset + 1] << 8);
        ULONG FieldType = Header & 0x3;
        ULONG Count = ((Header >> 4) & 0xF) + 1;
        ULONG InfoLength = Header >> 8;
        UCHAR OperatingClass = Body[Offset + 2];
        UCHAR Channel = Body[Offset + 3];
        ULONG Index;

        Offset += 4;
        if (InfoLength == 0 || Count * InfoLength > Length - Offset)
            break;

        if (FieldType == 0 && Ndis6RnrTbttHasBssid(InfoLength) &&
            OperatingClass >= NDIS6_RNR_6GHZ_CLASS_FIRST && OperatingClass <= NDIS6_RNR_6GHZ_CLASS_LAST)
        {
            for (Index = 0; Index < Count && Found < Capacity; Index++)
            {
                const UCHAR *Tbtt = Body + Offset + Index * InfoLength;
                ULONG Byte;

                for (Byte = 0; Byte < 6; Byte++)
                    Aps[Found].Bssid[Byte] = Tbtt[1 + Byte];
                Aps[Found].OperatingClass = OperatingClass;
                Aps[Found].Channel = Channel;
                Found++;
            }
        }

        Offset += Count * InfoLength;
    }

    return Found;
}

FORCEINLINE
ULONG
Ndis6RnrCollect6Ghz(
    _In_reads_bytes_(Length) const UCHAR *Ies,
    _In_ ULONG Length,
    _Out_writes_to_(Capacity, return) PNDIS6_RNR_AP Aps,
    _In_ ULONG Capacity)
{
    ULONG Offset = 0;
    ULONG Found = 0;

    while (Offset <= Length && Length - Offset >= 2 && Found < Capacity)
    {
        ULONG Id = Ies[Offset];
        ULONG ElementLength = Ies[Offset + 1];

        if (ElementLength > Length - Offset - 2)
            break;
        if (Id == NDIS6_RNR_ELEMENT_ID)
            Found += Ndis6RnrParseElement(Ies + Offset + 2, ElementLength, Aps + Found, Capacity - Found);
        Offset += 2 + ElementLength;
    }

    return Found;
}

#endif /* _NDIS6_RNR_H_ */
