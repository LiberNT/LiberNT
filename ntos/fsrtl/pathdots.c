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

/*
 * The names written so far sit after the root, if there is one, joined by
 * single separators. An empty name is a name like any other, so how many were
 * written has to be counted: the text alone cannot tell one empty name from
 * none.
 */
static
BOOLEAN
FsRtlpPopComponent(
    _In_reads_(*Written) PCWSTR Path,
    _In_ ULONG RootLength,
    _Inout_ PULONG Written,
    _Inout_ PULONG Components)
{
    ULONG Write = *Written;

    if (*Components == 0) return FALSE;

    while ((Write > RootLength) && (Path[Write - 1] != L'\\')) Write--;

    if (*Components > 1) Write--;

    *Written = Write;
    (*Components)--;

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
    ULONG RootLength = 0;
    ULONG Components = 0;
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
        RootLength = 1;
        Write = 1;
        Read = 1;
    }

    /* FIXME: only the cases the kmtest pins are verified on Windows 11; empty names are kept as they are */
    for (;;)
    {
        ULONG Start = Read;
        ULONG Length;
        ULONG i;

        while ((Read < Count) && (OriginalString[Read] != L'\\')) Read++;

        Length = Read - Start;

        if (FsRtlpIsDotDot(&OriginalString[Start], Length))
        {
            if (!FsRtlpPopComponent(OriginalString,
                                    RootLength,
                                    &Write,
                                    &Components))
            {
                return STATUS_IO_REPARSE_DATA_INVALID;
            }
        }
        else if (!FsRtlpIsDot(&OriginalString[Start], Length))
        {
            if (Components != 0) OriginalString[Write++] = L'\\';

            for (i = 0; i < Length; i++)
            {
                OriginalString[Write++] = OriginalString[Start + i];
            }

            Components++;
        }

        if (Read >= Count) break;

        Read++;
    }

    if (Write < Count) OriginalString[Write] = UNICODE_NULL;

    *NewLength = (USHORT)(Write * sizeof(WCHAR));

    return STATUS_SUCCESS;
}

/* EOF */
