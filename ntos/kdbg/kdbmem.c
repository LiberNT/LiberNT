/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Memory commands of the debugger beyond the plain dumps
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"
#include "kdbpriv.h"
#define NDEBUG
#include <debug.h>

#define KDB_MAXIMUM_PATTERN 16
#define KDB_COPY_CHUNK 64

/* The most one command goes over, so that a typo in a length does not run for hours */
#define KDB_MAXIMUM_RANGE 0x1000000

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
KdbpGetPattern(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _In_ ULONG First,
    _Out_writes_(KDB_MAXIMUM_PATTERN) PUCHAR Pattern,
    _Out_ PULONG Length)
{
    ULONG_PTR Value;
    ULONG Index;

    *Length = 0;

    for (Index = First; (Index < Argc) && (*Length < KDB_MAXIMUM_PATTERN); Index++)
    {
        if (!KdbpGetHexNumber(Argv[Index], &Value) || (Value > 0xFF))
        {
            KdbpPrint("%s is not a byte\n", Argv[Index]);
            return FALSE;
        }

        Pattern[(*Length)++] = (UCHAR)Value;
    }

    if (*Length == 0)
    {
        KdbPuts("At least one byte is needed\n");
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
KdbpDumpSymbols(
    _In_ ULONG Argc,
    _In_ PCHAR Argv[],
    _In_ ULONG Width)
{
    ULONG_PTR Address;
    ULONG_PTR Count = 8;
    ULONG_PTR Index;

    if (Argc < 2)
    {
        KdbPuts("An address is needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    if ((Argc > 2) && !KdbpGetCountExpression(Argv[2], &Count))
        return TRUE;

    if (Count > KDB_MAXIMUM_RANGE / Width)
    {
        KdbpPrint("At most 0x%lx bytes are shown at once\n", KDB_MAXIMUM_RANGE);
        return TRUE;
    }

    for (Index = 0; (Index < Count) && !KdbpIsOutputAborted(); Index++)
    {
        ULONG_PTR At = Address + Index * Width;
        ULONG_PTR Value = 0;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Value, (PVOID)At, Width)))
        {
            KdbpPrint("%p  ????\n", (PVOID)At);
            break;
        }

        KdbpPrint("%p  %p ", (PVOID)At, (PVOID)Value);
        KdbSymPrintAddress((PVOID)Value, NULL);
        KdbPuts("\n");
    }

    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
KdbpCmdNearestSymbol(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address;

    if (Argc < 2)
    {
        KdbPuts("An address is needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    KdbpPrint("%p ", (PVOID)Address);
    if (!KdbSymPrintAddress((PVOID)Address, NULL))
        KdbPuts("is in no loaded image");
    KdbPuts("\n");

    return TRUE;
}

BOOLEAN
KdbpCmdDumpUnicode(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR Address;
    ULONG_PTR Count = 64;
    ULONG_PTR Index;
    WCHAR Char;
    CHAR Text[2];

    if (Argc < 2)
    {
        KdbPuts("An address is needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Address))
        return TRUE;

    if ((Argc > 2) && !KdbpGetCountExpression(Argv[2], &Count))
        return TRUE;

    if ((Count == 0) || (Count > KDB_MAXIMUM_RANGE / sizeof(WCHAR)))
    {
        KdbpPrint("A string is from 1 to 0x%lx characters\n", (ULONG)(KDB_MAXIMUM_RANGE / sizeof(WCHAR)));
        return TRUE;
    }

    KdbpPrint("%p  \"", (PVOID)Address);

    for (Index = 0; (Index < Count) && !KdbpIsOutputAborted(); Index++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Char, (PVOID)(Address + Index * sizeof(WCHAR)), sizeof(Char))))
        {
            KdbPuts("\" ??\n");
            return TRUE;
        }

        if (Char == UNICODE_NULL)
            break;

        Text[0] = ((Char >= L' ') && (Char < 0x7F)) ? (CHAR)Char : '.';
        Text[1] = ANSI_NULL;
        KdbPuts(Text);
    }

    KdbPuts("\"\n");
    return TRUE;
}

BOOLEAN
KdbpCmdDumpDwordSymbols(
    ULONG Argc,
    PCHAR Argv[])
{
    return KdbpDumpSymbols(Argc, Argv, sizeof(ULONG));
}

BOOLEAN
KdbpCmdDumpQwordSymbols(
    ULONG Argc,
    PCHAR Argv[])
{
    return KdbpDumpSymbols(Argc, Argv, sizeof(ULONG64));
}

BOOLEAN
KdbpCmdDumpPointerSymbols(
    ULONG Argc,
    PCHAR Argv[])
{
    return KdbpDumpSymbols(Argc, Argv, sizeof(ULONG_PTR));
}

/*
 * One byte of what is being searched, read a page at a time. A page that
 * cannot be read is remembered as such, so the search goes past it at once.
 */
static
BOOLEAN
KdbpReadSearchByte(
    _In_ ULONG_PTR Address,
    _Out_ PUCHAR Byte)
{
    static UCHAR Page[PAGE_SIZE];
    static ULONG_PTR PageAddress = 1;
    static BOOLEAN PageReadable;
    ULONG_PTR Wanted = Address & ~(ULONG_PTR)(PAGE_SIZE - 1);

    if (Wanted != PageAddress)
    {
        PageAddress = Wanted;
        PageReadable = NT_SUCCESS(KdbpSafeReadMemory(Page, (PVOID)Wanted, PAGE_SIZE));
    }

    if (!PageReadable)
        return FALSE;

    *Byte = Page[Address & (PAGE_SIZE - 1)];
    return TRUE;
}

BOOLEAN
KdbpCmdSearchMemory(
    ULONG Argc,
    PCHAR Argv[])
{
    UCHAR Pattern[KDB_MAXIMUM_PATTERN];
    ULONG_PTR Address;
    ULONG_PTR Length;
    ULONG_PTR Offset;
    ULONG PatternLength;
    ULONG Matches = 0;
    ULONG Index;
    UCHAR Byte;

    if (Argc < 4)
    {
        KdbPuts("An address, a length and the bytes to look for are needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Address) ||
        !KdbpGetAddressExpression(Argv[2], &Length) ||
        !KdbpGetPattern(Argc, Argv, 3, Pattern, &PatternLength))
    {
        return TRUE;
    }

    if (Length > KDB_MAXIMUM_RANGE)
    {
        KdbpPrint("A search covers at most 0x%lx bytes\n", KDB_MAXIMUM_RANGE);
        return TRUE;
    }

    /* The page cache starts afresh: memory may have changed since the last search */
    KdbpReadSearchByte(0, &Byte);

    for (Offset = 0; (Offset + PatternLength <= Length) && !KdbpIsOutputAborted(); Offset++)
    {
        for (Index = 0; Index < PatternLength; Index++)
        {
            if (!KdbpReadSearchByte(Address + Offset + Index, &Byte) || (Byte != Pattern[Index]))
                break;
        }

        if (Index == PatternLength)
        {
            KdbpPrint("%p\n", (PVOID)(Address + Offset));
            Matches++;
        }
        else if ((Index == 0) && !KdbpReadSearchByte(Address + Offset, &Byte))
        {
            /* Nothing on a page that cannot be read can match */
            Offset = ((Address + Offset) | (PAGE_SIZE - 1)) - Address;
        }
    }

    KdbpPrint("%lu matches\n", Matches);
    return TRUE;
}

BOOLEAN
KdbpCmdCompareMemory(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG_PTR First;
    ULONG_PTR Second;
    ULONG_PTR Length;
    ULONG_PTR Offset;
    ULONG Differences = 0;
    UCHAR Left;
    UCHAR Right;

    if (Argc < 4)
    {
        KdbPuts("Two addresses and a length are needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &First) ||
        !KdbpGetAddressExpression(Argv[2], &Second) ||
        !KdbpGetAddressExpression(Argv[3], &Length))
    {
        return TRUE;
    }

    if (Length > KDB_MAXIMUM_RANGE)
    {
        KdbpPrint("A comparison covers at most 0x%lx bytes\n", KDB_MAXIMUM_RANGE);
        return TRUE;
    }

    for (Offset = 0; (Offset < Length) && !KdbpIsOutputAborted(); Offset++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Left, (PVOID)(First + Offset), sizeof(Left))) ||
            !NT_SUCCESS(KdbpSafeReadMemory(&Right, (PVOID)(Second + Offset), sizeof(Right))))
        {
            KdbpPrint("Offset 0x%Ix cannot be read\n", Offset);
            break;
        }

        if (Left != Right)
        {
            KdbpPrint("%p %02x  %p %02x\n", (PVOID)(First + Offset), Left, (PVOID)(Second + Offset), Right);
            Differences++;
        }
    }

    KdbpPrint("%lu differences\n", Differences);
    return TRUE;
}

BOOLEAN
KdbpCmdFillMemory(
    ULONG Argc,
    PCHAR Argv[])
{
    UCHAR Pattern[KDB_MAXIMUM_PATTERN];
    ULONG_PTR Address;
    ULONG_PTR Length;
    ULONG_PTR Offset;
    ULONG PatternLength;

    if (Argc < 4)
    {
        KdbPuts("An address, a length and the bytes to fill with are needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Address) ||
        !KdbpGetAddressExpression(Argv[2], &Length) ||
        !KdbpGetPattern(Argc, Argv, 3, Pattern, &PatternLength))
    {
        return TRUE;
    }

    if (Length > KDB_MAXIMUM_RANGE)
    {
        KdbpPrint("A fill covers at most 0x%lx bytes\n", KDB_MAXIMUM_RANGE);
        return TRUE;
    }

    for (Offset = 0; Offset < Length; Offset++)
    {
        if (!NT_SUCCESS(KdbpSafeWriteMemory((PVOID)(Address + Offset),
                                            &Pattern[Offset % PatternLength],
                                            sizeof(UCHAR))))
        {
            KdbpPrint("Offset 0x%Ix cannot be written\n", Offset);
            return TRUE;
        }
    }

    KdbpPrint("0x%Ix bytes filled\n", Length);
    return TRUE;
}

BOOLEAN
KdbpCmdMoveMemory(
    ULONG Argc,
    PCHAR Argv[])
{
    UCHAR Buffer[KDB_COPY_CHUNK];
    ULONG_PTR Source;
    ULONG_PTR Destination;
    ULONG_PTR Length;
    ULONG_PTR Done;
    ULONG Chunk;

    if (Argc < 4)
    {
        KdbPuts("A source, a destination and a length are needed\n");
        return TRUE;
    }

    if (!KdbpGetAddressExpression(Argv[1], &Source) ||
        !KdbpGetAddressExpression(Argv[2], &Destination) ||
        !KdbpGetAddressExpression(Argv[3], &Length))
    {
        return TRUE;
    }

    if (Length > KDB_MAXIMUM_RANGE)
    {
        KdbpPrint("A move covers at most 0x%lx bytes\n", KDB_MAXIMUM_RANGE);
        return TRUE;
    }

    /* Overlapping ranges are copied from the end when the destination is above the source */
    for (Done = 0; Done < Length; Done += Chunk)
    {
        ULONG_PTR Offset;

        Chunk = (ULONG)min(Length - Done, sizeof(Buffer));
        Offset = (Destination > Source) ? (Length - Done - Chunk) : Done;

        if (!NT_SUCCESS(KdbpSafeReadMemory(Buffer, (PVOID)(Source + Offset), Chunk)) ||
            !NT_SUCCESS(KdbpSafeWriteMemory((PVOID)(Destination + Offset), Buffer, Chunk)))
        {
            KdbpPrint("Offset 0x%Ix cannot be copied\n", Offset);
            return TRUE;
        }
    }

    KdbpPrint("0x%Ix bytes moved\n", Length);
    return TRUE;
}

/* EOF */
