/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Walking a path's dots away so what is left names one file
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
FsRtlpIsDot(
    _In_reads_(Length) PCWSTR Component,
    _In_ ULONG Length)
{
    return (BOOLEAN)((Length == 1) && (Component[0] == L'.'));
}

static
BOOLEAN
FsRtlpIsDotDot(
    _In_reads_(Length) PCWSTR Component,
    _In_ ULONG Length)
{
    return (BOOLEAN)((Length == 2) && (Component[0] == L'.') &&
                     (Component[1] == L'.'));
}

static
BOOLEAN
FsRtlpPopComponent(
    _Inout_updates_(*Written) PWSTR Path,
    _Inout_ PULONG Written)
{
    ULONG Write = *Written;
    BOOLEAN Rooted = (BOOLEAN)((Write != 0) && (Path[0] == L'\\'));

    if (Write == 0) return FALSE;
    if (Rooted && (Write == 1)) return FALSE;

    while ((Write != 0) && (Path[Write - 1] != L'\\')) Write--;

    if (Write > 1) Write--;
    else if ((Write == 1) && !Rooted) Write = 0;

    *Written = Write;

    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlRemoveDotsFromPath(
    _Inout_updates_bytes_(PathLength) PWSTR OriginalString,
    _In_ USHORT PathLength,
    _Out_ USHORT *NewLength)
{
    ULONG Count = PathLength / sizeof(WCHAR);
    ULONG Read = 0;
    ULONG Write = 0;

    PAGED_CODE();

    if ((OriginalString == NULL) || (NewLength == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    *NewLength = 0;

    if (FlagOn(PathLength, 1)) return STATUS_INVALID_PARAMETER;

    if (Count == 0) return STATUS_SUCCESS;

    if (OriginalString[0] == L'\\')
    {
        Write = 1;
        Read = 1;
    }

    while (Read < Count)
    {
        ULONG Start = Read;
        ULONG Length;
        ULONG i;

        while ((Read < Count) && (OriginalString[Read] != L'\\')) Read++;

        Length = Read - Start;

        if (Read < Count) Read++;

        if (Length == 0) continue;

        if (FsRtlpIsDot(&OriginalString[Start], Length)) continue;

        if (FsRtlpIsDotDot(&OriginalString[Start], Length))
        {
            if (!FsRtlpPopComponent(OriginalString, &Write))
            {
                return STATUS_INVALID_PARAMETER;
            }

            continue;
        }

        if ((Write != 0) && (OriginalString[Write - 1] != L'\\'))
        {
            OriginalString[Write++] = L'\\';
        }

        for (i = 0; i < Length; i++)
        {
            OriginalString[Write++] = OriginalString[Start + i];
        }
    }

    *NewLength = (USHORT)(Write * sizeof(WCHAR));

    return STATUS_SUCCESS;
}

/* EOF */
