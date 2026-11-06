/*
 * PROJECT:     LiberNT Desktop Window Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Converts a row of a client swap chain format to BGRA8 for software composition
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#include <math.h>
#include <string.h>

static __inline float
DwmDxHalfToFloat(unsigned short Half)
{
    int Exponent = (Half >> 10) & 0x1f;
    int Mantissa = Half & 0x3ff;
    float Value = Exponent != 0 ? (float)ldexp(1.0 + Mantissa / 1024.0, Exponent - 15)
                                : (float)ldexp((double)Mantissa, -24);

    return (Half & 0x8000) != 0 ? -Value : Value;
}

static __inline unsigned char
DwmDxUnormToByte(float Value)
{
    return Value >= 1.0f ? 255 : Value > 0.0f ? (unsigned char)(Value * 255.0f + 0.5f) : 0;
}

static __inline unsigned char
DwmDxLinearToSrgbByte(float Value)
{
    if (Value >= 1.0f)
        return 255;
    if (!(Value > 0.0f))
        return 0;
    return DwmDxUnormToByte(Value <= 0.0031308f ? Value * 12.92f : 1.055f * (float)pow((double)Value, 1.0 / 2.4) - 0.055f);
}

static __inline void
DwmDxConvertRow(unsigned long Format, const unsigned char *Source, unsigned char *Destination, unsigned long Width)
{
    unsigned long X;

    for (X = 0; X < Width; ++X, Destination += 4)
    {
        if (Format == DWM_DX_FORMAT_R16G16B16A16_FLOAT)
        {
            unsigned short Channel[4];

            memcpy(Channel, Source + X * sizeof(Channel), sizeof(Channel));
            Destination[0] = DwmDxLinearToSrgbByte(DwmDxHalfToFloat(Channel[2]));
            Destination[1] = DwmDxLinearToSrgbByte(DwmDxHalfToFloat(Channel[1]));
            Destination[2] = DwmDxLinearToSrgbByte(DwmDxHalfToFloat(Channel[0]));
            Destination[3] = DwmDxUnormToByte(DwmDxHalfToFloat(Channel[3]));
        }
        else if (Format == DWM_DX_FORMAT_R10G10B10A2_UNORM)
        {
            unsigned int Pixel;

            memcpy(&Pixel, Source + X * sizeof(Pixel), sizeof(Pixel));
            Destination[0] = (unsigned char)((((Pixel >> 20) & 0x3ff) * 255 + 511) / 1023);
            Destination[1] = (unsigned char)((((Pixel >> 10) & 0x3ff) * 255 + 511) / 1023);
            Destination[2] = (unsigned char)(((Pixel & 0x3ff) * 255 + 511) / 1023);
            Destination[3] = (unsigned char)((Pixel >> 30) * 85);
        }
        else if (Format == DWM_DX_FORMAT_R8G8B8A8_UNORM)
        {
            Destination[0] = Source[X * 4 + 2];
            Destination[1] = Source[X * 4 + 1];
            Destination[2] = Source[X * 4];
            Destination[3] = Source[X * 4 + 3];
        }
        else
        {
            memcpy(Destination, Source + X * 4, 4);
        }
    }
}
