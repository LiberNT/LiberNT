/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Names of the entries of an ISO 9660 directory record
 * COPYRIGHT:   Copyright 2026 Zhou Ji Yang <yinglanxing@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <freeldr.h>
#include <disk.h>
#include <fs/iso.h>

/* FUNCTIONS *****************************************************************/

BOOLEAN
IsoExtractName(
    _In_ PDIR_RECORD Record,
    _Out_writes_(NameBufferSize) PCHAR NameBuffer,
    _In_ SIZE_T NameBufferSize)
{
    SIZE_T Index;
    SIZE_T SuOffset;
    SIZE_T NameLength = 0;
    BOOLEAN HaveSuspName = FALSE;
    BOOLEAN MoreEntries = TRUE;

    if (!Record || !NameBuffer || NameBufferSize == 0)
        return FALSE;

    /* Skip '.' and '..' entries early */
    if (Record->FileIdLength == 1 && (Record->FileId[0] == 0 || Record->FileId[0] == 1))
        return FALSE;

    /*
     * The ISO 9660 identifier is lossy: mastering tools fold it to upper
     * case, replace the dots and spaces they cannot represent and truncate
     * it. Rock Ridge carries the name the source tree actually used, so
     * prefer its alternate name (SUSP "NM") when the record has one.
     */
    SuOffset = FIELD_OFFSET(DIR_RECORD, FileId) + Record->FileIdLength;
    if ((Record->FileIdLength & 1) == 0)
        SuOffset++;

    while (MoreEntries && SuOffset + 4 <= Record->RecordLength)
    {
        PCHAR Entry = (PCHAR)Record + SuOffset;
        UCHAR EntryLength = (UCHAR)Entry[2];
        UCHAR Flags;

        if (EntryLength < 4 || SuOffset + EntryLength > Record->RecordLength)
            break;

        if (Entry[0] != 'N' || Entry[1] != 'M' || EntryLength < 5)
        {
            SuOffset += EntryLength;
            continue;
        }

        Flags = (UCHAR)Entry[4];
        SuOffset += EntryLength;

        /* CURRENT and PARENT are not real names */
        if (Flags & 0x06)
            continue;

        MoreEntries = !!(Flags & 0x01);
        for (Index = 5; Index < EntryLength && NameLength < NameBufferSize - 1; ++Index)
            NameBuffer[NameLength++] = Entry[Index];
        HaveSuspName = TRUE;
    }

    if (HaveSuspName && NameLength != 0)
    {
        NameBuffer[NameLength] = '\0';
        return TRUE;
    }

    for (Index = 0; Index < Record->FileIdLength && Index < NameBufferSize - 1; ++Index)
    {
        CHAR Character = Record->FileId[Index];

        if (Character == ';')
            break;

        NameBuffer[Index] = Character;
    }

    NameBuffer[Index] = '\0';

    /* Remove trailing dot, if any (appears on directory records) */
    while (Index > 0 && NameBuffer[Index - 1] == '.')
    {
        NameBuffer[Index - 1] = '\0';
        --Index;
    }

    if (Index == 0)
        return FALSE;

    return TRUE;
}
