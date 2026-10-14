/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     System identifier and monitor description from firmware tables
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "winload.h"

#define SMBIOS_ANCHOR_64            "_SM3_"
#define SMBIOS_ANCHOR_32            "_SM_"
#define SMBIOS_64_TABLE_SIZE_OFFSET 0x0C
#define SMBIOS_64_TABLE_OFFSET      0x10
#define SMBIOS_32_TABLE_SIZE_OFFSET 0x16
#define SMBIOS_32_TABLE_OFFSET      0x18
#define SMBIOS_HEADER_SIZE          4
#define SMBIOS_TYPE_SYSTEM          1
#define SMBIOS_TYPE_END             127
#define SMBIOS_SYSTEM_MANUFACTURER  4
#define SMBIOS_SYSTEM_PRODUCT       5
#define SMBIOS_SYSTEM_MIN_LENGTH    6

#define EDID_SIZE                   128
#define EDID_TIMING_OFFSET          54
#define EDID_WIDTH_CM_OFFSET        21
#define EDID_HEIGHT_CM_OFFSET       22
#define EDID_CLOCK_UNIT             10000
#define EDID_MONITOR_VERSION        1

static const UCHAR EdidHeader[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

static const PCSTR SmbiosPlaceholders[] =
{
    "To Be Filled By O.E.M.",
    "Default string",
    "System Product Name",
    "System manufacturer",
    "Not Specified",
    "Unknown",
    "Unknown Product",
};

static
PCSTR
SmbiosGetString(
    _In_ const UCHAR* Structure,
    _In_ const UCHAR* End,
    _In_ UCHAR Index)
{
    PCSTR String = (PCSTR)(Structure + Structure[1]);
    ULONG i;

    if (Index == 0)
        return NULL;

    for (; Index > 1; Index--)
    {
        while ((const UCHAR*)String < End && *String)
            String++;

        String++;
        if ((const UCHAR*)String >= End || *String == ANSI_NULL)
            return NULL;
    }

    if ((const UCHAR*)String >= End || memchr(String, 0, End - (const UCHAR*)String) == NULL)
        return NULL;

    for (i = 0; i < RTL_NUMBER_OF(SmbiosPlaceholders); i++)
    {
        if (strcmp(String, SmbiosPlaceholders[i]) == 0)
            return NULL;
    }

    return *String ? String : NULL;
}

BOOLEAN
OslGetSystemIdentifier(
    _Out_writes_z_(Size) PSTR Buffer,
    _In_ SIZE_T Size)
{
    const UCHAR* Entry = FwGetSmbiosEntryPoint();
    const UCHAR* Structure;
    const UCHAR* End;
    const UCHAR* Next;
    PCSTR Manufacturer, Product;

    if (!Entry)
        return FALSE;

    if (RtlEqualMemory(Entry, SMBIOS_ANCHOR_64, sizeof(SMBIOS_ANCHOR_64) - 1))
    {
        Structure = (const UCHAR*)(ULONG_PTR)LeUlonglong(Entry + SMBIOS_64_TABLE_OFFSET);
        End = Structure + LeUlong(Entry + SMBIOS_64_TABLE_SIZE_OFFSET);
    }
    else if (RtlEqualMemory(Entry, SMBIOS_ANCHOR_32, sizeof(SMBIOS_ANCHOR_32) - 1))
    {
        Structure = (const UCHAR*)(ULONG_PTR)LeUlong(Entry + SMBIOS_32_TABLE_OFFSET);
        End = Structure + (Entry[SMBIOS_32_TABLE_SIZE_OFFSET] | (Entry[SMBIOS_32_TABLE_SIZE_OFFSET + 1] << 8));
    }
    else
    {
        return FALSE;
    }

    while (Structure &&
           Structure + SMBIOS_HEADER_SIZE <= End &&
           Structure[1] >= SMBIOS_HEADER_SIZE &&
           Structure + Structure[1] < End &&
           Structure[0] != SMBIOS_TYPE_END)
    {
        if (Structure[0] == SMBIOS_TYPE_SYSTEM)
        {
            if (Structure[1] < SMBIOS_SYSTEM_MIN_LENGTH)
                return FALSE;

            Manufacturer = SmbiosGetString(Structure, End, Structure[SMBIOS_SYSTEM_MANUFACTURER]);
            Product = SmbiosGetString(Structure, End, Structure[SMBIOS_SYSTEM_PRODUCT]);
            if (!Manufacturer || !Product)
                return FALSE;

            if (strstr(Product, Manufacturer))
                return NT_SUCCESS(RtlStringCbCopyA(Buffer, Size, Product));

            return NT_SUCCESS(RtlStringCbPrintfA(Buffer, Size, "%s %s", Manufacturer, Product));
        }

        for (Next = Structure + Structure[1]; Next + 1 < End && (Next[0] || Next[1]); Next++)
            ;

        Structure = Next + 2;
    }

    return FALSE;
}

BOOLEAN
OslGetMonitorData(
    _Out_ PCM_MONITOR_DEVICE_DATA Monitor,
    _Out_ PULONG RefreshRate)
{
    UCHAR Edid[EDID_SIZE];
    const UCHAR* Timing = Edid + EDID_TIMING_OFFSET;
    ULONG Clock, HActive, HBlank, VActive, VBlank;
    ULONG HFront, HSync, HBack, VFront, VSync, VBack, Width, Height;
    ULONGLONG Total;

    if (!FwGetEdid(Edid, sizeof(Edid)) || !RtlEqualMemory(Edid, EdidHeader, sizeof(EdidHeader)))
        return FALSE;

    Clock = Timing[0] | (Timing[1] << 8);
    HActive = Timing[2] | ((Timing[4] & 0xF0) << 4);
    HBlank = Timing[3] | ((Timing[4] & 0x0F) << 8);
    VActive = Timing[5] | ((Timing[7] & 0xF0) << 4);
    VBlank = Timing[6] | ((Timing[7] & 0x0F) << 8);
    HFront = Timing[8] | ((Timing[11] & 0xC0) << 2);
    HSync = Timing[9] | ((Timing[11] & 0x30) << 4);
    VFront = (Timing[10] >> 4) | ((Timing[11] & 0x0C) << 2);
    VSync = (Timing[10] & 0x0F) | ((Timing[11] & 0x03) << 4);
    Width = Timing[12] | ((Timing[14] & 0xF0) << 4);
    Height = Timing[13] | ((Timing[14] & 0x0F) << 8);

    if (Clock == 0 ||
        HActive == 0 ||
        VActive == 0 ||
        HBlank < HFront + HSync ||
        VBlank < VFront + VSync)
    {
        return FALSE;
    }

    HBack = HBlank - HFront - HSync;
    VBack = VBlank - VFront - VSync;
    if (Width == 0)
        Width = Edid[EDID_WIDTH_CM_OFFSET] * 10;
    if (Height == 0)
        Height = Edid[EDID_HEIGHT_CM_OFFSET] * 10;

    RtlZeroMemory(Monitor, sizeof(*Monitor));
    Monitor->Version = EDID_MONITOR_VERSION;
    Monitor->HorizontalScreenSize = (USHORT)Width;
    Monitor->VerticalScreenSize = (USHORT)Height;
    Monitor->HorizontalResolution = (USHORT)HActive;
    Monitor->VerticalResolution = (USHORT)VActive;
    Monitor->HorizontalDisplayTimeLow = (USHORT)HActive;
    Monitor->HorizontalDisplayTime = (USHORT)HActive;
    Monitor->HorizontalDisplayTimeHigh = (USHORT)HActive;
    Monitor->HorizontalBackPorchLow = (USHORT)HBack;
    Monitor->HorizontalBackPorch = (USHORT)HBack;
    Monitor->HorizontalBackPorchHigh = (USHORT)HBack;
    Monitor->HorizontalFrontPorchLow = (USHORT)HFront;
    Monitor->HorizontalFrontPorch = (USHORT)HFront;
    Monitor->HorizontalFrontPorchHigh = (USHORT)HFront;
    Monitor->HorizontalSyncLow = (USHORT)HSync;
    Monitor->HorizontalSync = (USHORT)HSync;
    Monitor->HorizontalSyncHigh = (USHORT)HSync;
    Monitor->VerticalBackPorchLow = (USHORT)VBack;
    Monitor->VerticalBackPorch = (USHORT)VBack;
    Monitor->VerticalBackPorchHigh = (USHORT)VBack;
    Monitor->VerticalFrontPorchLow = (USHORT)VFront;
    Monitor->VerticalFrontPorch = (USHORT)VFront;
    Monitor->VerticalFrontPorchHigh = (USHORT)VFront;
    Monitor->VerticalSyncLow = (USHORT)VSync;
    Monitor->VerticalSync = (USHORT)VSync;
    Monitor->VerticalSyncHigh = (USHORT)VSync;

    Total = (ULONGLONG)(HActive + HBlank) * (VActive + VBlank);
    *RefreshRate = (ULONG)(((ULONGLONG)Clock * EDID_CLOCK_UNIT + Total / 2) / Total);
    return *RefreshRate != 0;
}
