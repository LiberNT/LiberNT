/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     F2FS support for FreeLoader
 */

#pragma once

ULONGLONG
F2FsGetVolumeSize(
    _In_ ULONG DeviceId);

const DEVVTBL* F2FsMount(ULONG DeviceId);
