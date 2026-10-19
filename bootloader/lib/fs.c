/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     File access on a block device
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include "ntfs.h"

static DK_DEVICE FsMountedDevice;
static PNTFS_VOLUME_INFO FsMountedVolume;
static BOOLEAN FsMountedExFat;

PVOID (*FsMemoryReader)(PCSTR Path, PULONG Size);

static
BOOLEAN
FsMount(
    _In_ PDK_DEVICE Device)
{
    if ((!FsMountedVolume && !FsMountedExFat) ||
        FsMountedDevice.Firmware != Device->Firmware ||
        FsMountedDevice.Offset != Device->Offset)
    {
        FsMountedVolume = NtfsMount(Device);
        FsMountedExFat = !FsMountedVolume && ExFatMount(Device);
        if (!FsMountedVolume && !FsMountedExFat)
            return FALSE;

        FsMountedDevice = *Device;
    }

    return TRUE;
}

PVOID
FsReadFileA(
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Path,
    _Out_ PULONG Size)
{
    *Size = 0;

    if (Device->Memory)
        return FsMemoryReader ? FsMemoryReader(Path, Size) : NULL;

    if (!FsMount(Device))
        return NULL;

    return FsMountedVolume ? NtfsReadFile(FsMountedVolume, Path, Size) : ExFatReadFile(Path, Size);
}

PVOID
FsOpenFileA(
    _In_ PDK_DEVICE Device,
    _In_ PCSTR Path,
    _Out_ PULONGLONG Size)
{
    *Size = 0;

    if (Device->Memory || !FsMount(Device) || !FsMountedVolume)
        return NULL;

    return NtfsOpenFile(FsMountedVolume, Path, Size);
}

BOOLEAN
FsReadFileAt(
    _In_ PVOID File,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    return NtfsReadFileAt(File, Offset, Buffer, Length);
}

VOID
FsCloseFile(
    _In_ PVOID File)
{
    NtfsCloseFile(File);
}

PVOID
FsReadFile(
    _In_ PDK_DEVICE Device,
    _In_ PCWSTR Path,
    _Out_ PULONG Size)
{
    CHAR AnsiPath[BM_PATH_CHARS];
    ULONG i;

    *Size = 0;

    for (i = 0; Path[i] != UNICODE_NULL; i++)
    {
        if (i >= RTL_NUMBER_OF(AnsiPath) - 1 || Path[i] > 0x7F)
            return NULL;
        AnsiPath[i] = (CHAR)Path[i];
    }
    AnsiPath[i] = ANSI_NULL;

    return FsReadFileA(Device, AnsiPath, Size);
}

PCWSTR
FsGetDriverName(
    _In_ PDK_DEVICE Device)
{
    if (Device->Memory)
        return L"fastfat";

    if (FsMountedExFat)
        return ExFatIsExFat() ? L"exfat" : L"fastfat";

    return L"ntfs";
}
