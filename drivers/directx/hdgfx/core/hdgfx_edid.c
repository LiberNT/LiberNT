/*
 * PROJECT:     LiberNT Intel HD Graphics Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Detailed timing descriptor and EDID base block parsing
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include "hdgfx_core.h"

#include <string.h>

#define EDID_MANUFACTURER       8
#define EDID_PRODUCT_CODE       10
#define EDID_SERIAL_NUMBER      12
#define EDID_VERSION            18
#define EDID_REVISION           19
#define EDID_WIDTH_CM           21
#define EDID_HEIGHT_CM          22
#define EDID_DESCRIPTORS        54
#define EDID_DESCRIPTOR_SIZE    18
#define EDID_DESCRIPTOR_COUNT   4
#define EDID_EXTENSIONS         126

static uint32_t
Read16(
    const uint8_t *Data)
{
    return Data[0] | ((uint32_t)Data[1] << 8);
}

void
HdgfxDecodeDtd(
    const uint8_t *Dtd,
    HDGFX_DTD *Result)
{
    uint32_t HActive = Dtd[2] | ((uint32_t)(Dtd[4] >> 4) << 8);
    uint32_t HBlank = Dtd[3] | ((uint32_t)(Dtd[4] & 0xF) << 8);
    uint32_t VActive = Dtd[5] | ((uint32_t)(Dtd[7] >> 4) << 8);
    uint32_t VBlank = Dtd[6] | ((uint32_t)(Dtd[7] & 0xF) << 8);
    uint32_t HSyncOffset = Dtd[8] | ((uint32_t)((Dtd[11] >> 6) & 3) << 8);
    uint32_t HSyncWidth = Dtd[9] | ((uint32_t)((Dtd[11] >> 4) & 3) << 8);
    uint32_t VSyncOffset = (Dtd[10] >> 4) | ((uint32_t)((Dtd[11] >> 2) & 3) << 4);
    uint32_t VSyncWidth = (Dtd[10] & 0xF) | ((uint32_t)(Dtd[11] & 3) << 4);
    uint32_t Flags = Dtd[17];

    Result->DotKHz = Read16(Dtd) * 10;
    Result->Timings.HActive = HActive;
    Result->Timings.HBlankStart = HActive;
    Result->Timings.HBlankEnd = HActive + HBlank;
    Result->Timings.HSyncStart = HActive + HSyncOffset;
    Result->Timings.HSyncEnd = HActive + HSyncOffset + HSyncWidth;
    Result->Timings.HTotal = HActive + HBlank;
    Result->Timings.VActive = VActive;
    Result->Timings.VBlankStart = VActive;
    Result->Timings.VBlankEnd = VActive + VBlank;
    Result->Timings.VSyncStart = VActive + VSyncOffset;
    Result->Timings.VSyncEnd = VActive + VSyncOffset + VSyncWidth;
    Result->Timings.VTotal = VActive + VBlank;
    Result->WidthMm = Dtd[12] | ((uint32_t)(Dtd[14] >> 4) << 8);
    Result->HeightMm = Dtd[13] | ((uint32_t)(Dtd[14] & 0xF) << 8);
    Result->HSyncPositive = (Flags & 0x02) != 0;
    Result->VSyncPositive = (Flags & 0x04) != 0;
}

bool
HdgfxEdidParse(
    const uint8_t *Edid,
    HDGFX_EDID *Result)
{
    static const uint8_t Header[8] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
    uint32_t Manufacturer;
    uint32_t Index;
    uint8_t Sum = 0;

    memset(Result, 0, sizeof(*Result));
    if (memcmp(Edid, Header, sizeof(Header)) != 0)
        return false;
    for (Index = 0; Index < HDGFX_EDID_SIZE; Index++)
        Sum = (uint8_t)(Sum + Edid[Index]);
    if (Sum != 0)
        return false;

    Manufacturer = ((uint32_t)Edid[EDID_MANUFACTURER] << 8) | Edid[EDID_MANUFACTURER + 1];
    Result->Manufacturer[0] = (char)('@' + ((Manufacturer >> 10) & 0x1F));
    Result->Manufacturer[1] = (char)('@' + ((Manufacturer >> 5) & 0x1F));
    Result->Manufacturer[2] = (char)('@' + (Manufacturer & 0x1F));
    Result->ProductCode = Read16(Edid + EDID_PRODUCT_CODE);
    Result->SerialNumber = Read16(Edid + EDID_SERIAL_NUMBER) |
                           (Read16(Edid + EDID_SERIAL_NUMBER + 2) << 16);
    Result->Version = Edid[EDID_VERSION];
    Result->Revision = Edid[EDID_REVISION];
    Result->WidthCm = Edid[EDID_WIDTH_CM];
    Result->HeightCm = Edid[EDID_HEIGHT_CM];
    Result->Extensions = Edid[EDID_EXTENSIONS];

    for (Index = 0; Index < EDID_DESCRIPTOR_COUNT; Index++)
    {
        const uint8_t *Descriptor = Edid + EDID_DESCRIPTORS + Index * EDID_DESCRIPTOR_SIZE;

        if (Read16(Descriptor) == 0)
            continue;
        HdgfxDecodeDtd(Descriptor, &Result->Dtds[Result->DtdCount++]);
    }
    return true;
}
