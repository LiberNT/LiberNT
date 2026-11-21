/*
 * PROJECT:     LiberNT Media Foundation decoders
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Runtime binding of the FFmpeg libraries installed by the second stage
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define FFMPEG_RUNTIME_STRING_(x) #x
#define FFMPEG_RUNTIME_STRING(x) FFMPEG_RUNTIME_STRING_(x)
#define FFMPEG_RUNTIME_LIBRARY(name, major) L"" name "-" FFMPEG_RUNTIME_STRING(major) ".dll"

typedef struct _FFMPEG_RUNTIME_IMPORT
{
    PCSTR Name;
    PVOID *Address;
} FFMPEG_RUNTIME_IMPORT;

static BOOL
FFmpegRuntimeBind(
    _In_reads_(LibraryCount) PCWSTR const *Libraries,
    _In_ ULONG LibraryCount,
    _In_reads_(ImportCount) const FFMPEG_RUNTIME_IMPORT *Imports,
    _In_ ULONG ImportCount)
{
    HMODULE Modules[8];
    ULONG Index, Library;

    if (LibraryCount > ARRAYSIZE(Modules))
        return FALSE;

    for (Index = 0; Index < LibraryCount; Index++)
    {
        Modules[Index] = LoadLibraryExW(Libraries[Index], NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!Modules[Index])
        {
            while (Index--)
                FreeLibrary(Modules[Index]);
            return FALSE;
        }
    }

    for (Index = 0; Index < ImportCount; Index++)
    {
        *Imports[Index].Address = NULL;
        for (Library = 0; Library < LibraryCount && !*Imports[Index].Address; Library++)
            *Imports[Index].Address = (PVOID)GetProcAddress(Modules[Library], Imports[Index].Name);

        if (!*Imports[Index].Address)
        {
            for (Library = 0; Library < LibraryCount; Library++)
                FreeLibrary(Modules[Library]);
            return FALSE;
        }
    }

    return TRUE;
}
