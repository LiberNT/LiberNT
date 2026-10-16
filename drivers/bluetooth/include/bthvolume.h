/*
 * PROJECT:     LiberNT Bluetooth stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AVRCP absolute volume exchange between the A2DP audio driver and the AVRCP driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define BTH_VOLUME_SET_CALLBACK     L"\\Callback\\LiberNTBthAbsoluteVolumeSet"
#define BTH_VOLUME_STATE_CALLBACK   L"\\Callback\\LiberNTBthAbsoluteVolumeState"
#define BTH_VOLUME_MAXIMUM_LEVEL    0x7F

typedef struct _BTH_ABSOLUTE_VOLUME
{
    BTH_ADDR Address;
    BOOLEAN Supported;
    UCHAR Level;
} BTH_ABSOLUTE_VOLUME, *PBTH_ABSOLUTE_VOLUME;

static __inline NTSTATUS
BthOpenVolumeCallback(
    _In_ PCWSTR Name,
    _Out_ PCALLBACK_OBJECT *Callback)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING String;

    RtlInitUnicodeString(&String, Name);
    InitializeObjectAttributes(&Attributes, &String, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE | OBJ_PERMANENT, NULL, NULL);
    return ExCreateCallback(Callback, &Attributes, TRUE, TRUE);
}
