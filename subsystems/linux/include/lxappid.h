/*
 * PROJECT:     LiberNT Linux Subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Application user model IDs of Linux windows and terminals
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define LX_APP_ID_MAX       128
#define LX_APP_ID_FORMAT    L"LiberNT.Linux.%08lX.App.%s"
#define LX_TERMINAL_ID_FORMAT L"LiberNT.Linux.%08lX.Terminal"

static __inline
BOOL
LxNormalizeRoot(
    _In_ PCWSTR Root,
    _Out_writes_(Count) PWSTR Normalized,
    _In_ DWORD Count)
{
    DWORD Length = GetFullPathNameW(Root, Count, Normalized, NULL);

    if ((Length == 0) || (Length >= Count))
        return FALSE;
    while ((Length > 3) && (Normalized[Length - 1] == L'\\'))
        Normalized[--Length] = UNICODE_NULL;
    CharLowerBuffW(Normalized, Length);
    return TRUE;
}

static __inline
ULONG
LxRootTag(
    _In_ PCWSTR NormalizedRoot)
{
    ULONG Hash = 2166136261u;

    for (; *NormalizedRoot != UNICODE_NULL; NormalizedRoot++)
    {
        Hash = (Hash ^ (*NormalizedRoot & 0xFF)) * 16777619u;
        Hash = (Hash ^ (*NormalizedRoot >> 8)) * 16777619u;
    }
    return Hash;
}
