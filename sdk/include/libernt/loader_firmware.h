/*
 * PROJECT:     LiberNT Loader/Kernel Handoff
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Entries of EFI_FIRMWARE_INFORMATION.FirmwareResourceList (one per ESRT entry)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef struct _LOADER_FIRMWARE_RESOURCE
{
    LIST_ENTRY ListEntry;
    GUID FirmwareClass;
    ULONG FirmwareType;
    ULONG FirmwareVersion;
    ULONG LowestSupportedFirmwareVersion;
    ULONG CapsuleFlags;
    ULONG LastAttemptVersion;
    ULONG LastAttemptStatus;
} LOADER_FIRMWARE_RESOURCE, *PLOADER_FIRMWARE_RESOURCE;
