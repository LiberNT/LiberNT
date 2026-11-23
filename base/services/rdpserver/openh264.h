/*
 * PROJECT:     LiberNT RDP console server
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Download, check and remove Cisco's OpenH264 binary for the RDP console server
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENH264_PARAMETERS_KEY L"SYSTEM\\CurrentControlSet\\Services\\LiberNTRDPServer\\Parameters"
#define OPENH264_VALUE L"OpenH264"
#define OPENH264_MAX_PACKED (8u * 1024 * 1024)
#define OPENH264_MAX_UNPACKED (16u * 1024 * 1024)

typedef struct OPENH264_BINARY
{
    USHORT Architecture;
    const WCHAR *Name;
    BYTE Sha256[32];
} OPENH264_BINARY;

const OPENH264_BINARY *OpenH264Binary(USHORT architecture);
BOOL OpenH264Sha256(const BYTE *data, DWORD size, BYTE hash[32]);
DWORD OpenH264Unpack(const BYTE *packed, DWORD packedSize, const BYTE sha256[32], BYTE **unpacked, DWORD *unpackedSize);
BOOL OpenH264Enabled(void);
HMODULE OpenH264Load(void);
DWORD OpenH264Install(void);
DWORD OpenH264Remove(void);

#ifdef __cplusplus
}
#endif
