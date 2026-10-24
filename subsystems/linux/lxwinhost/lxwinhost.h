/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows host for the windows of Linux Wayland programs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include "resource.h"

#define LX_MAX_ICON     256

PUCHAR LxBuildIcon(_In_ PLX_WIN_ICON_MSG Message, _In_ ULONG Size, _Out_ PULONG IconSize);
HICON LxCreateIconFromIco(_In_reads_bytes_(Size) const UCHAR *Icon, _In_ ULONG Size, _In_ int Desired);
VOID LxIconCacheInitialize(_In_ PCWSTR Root);
BOOL LxIconCacheLoad(_In_reads_bytes_(AppIdLength) PCSTR AppId, _In_ ULONG AppIdLength, _Out_ HICON *BigIcon,
                     _Out_ HICON *SmallIcon);
VOID LxIconCacheStore(_In_reads_bytes_(AppIdLength) PCSTR AppId, _In_ ULONG AppIdLength,
                      _In_reads_bytes_(IconSize) const UCHAR *Icon, _In_ ULONG IconSize);
