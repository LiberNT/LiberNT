/*
 * PROJECT:     LiberNT Compressed Symbol Table Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Lookup in the .symz image section without memory allocation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdef.h>
#include <symz.h>

#include "LzmaDec.h"

typedef struct _SYMZ_ARENA
{
    ISzAlloc Alloc;
    PUCHAR Base;
    SIZE_T Size;
    SIZE_T Used;
} SYMZ_ARENA, *PSYMZ_ARENA;

static
void *
SymzArenaAlloc(
    ISzAllocPtr Alloc,
    size_t Size)
{
    PSYMZ_ARENA Arena = (PSYMZ_ARENA)Alloc;
    SIZE_T Aligned = (Arena->Used + sizeof(PVOID) - 1) & ~(SIZE_T)(sizeof(PVOID) - 1);
    PVOID Block;

    if (Aligned > Arena->Size || Size > Arena->Size - Aligned)
        return NULL;

    Block = Arena->Base + Aligned;
    Arena->Used = Aligned + Size;
    return Block;
}

static
void
SymzArenaFree(
    ISzAllocPtr Alloc,
    void *Address)
{
}

BOOLEAN
SymzCheckHeader(
    const SYMZ_HEADER *Header,
    ULONG TableSize)
{
    ULONG Available;

    if (TableSize < sizeof(SYMZ_HEADER) ||
        Header->Magic != SYMZ_MAGIC ||
        Header->Version != SYMZ_VERSION ||
        Header->HeaderSize < sizeof(SYMZ_HEADER) ||
        Header->HeaderSize > TableSize ||
        Header->MaxUnpackedSize > SYMZ_MAX_BLOCK_SIZE)
    {
        return FALSE;
    }

    Available = (TableSize - Header->HeaderSize) / sizeof(SYMZ_BLOCK);
    if (Header->SymbolBlockCount > Available)
        return FALSE;
    Available -= Header->SymbolBlockCount;
    if (Header->LineBlockCount > Available)
        return FALSE;
    Available -= Header->LineBlockCount;
    return Header->FileBlockCount <= Available;
}

BOOLEAN
SymzDecodeBlock(
    const SYMZ_HEADER *Header,
    const VOID *Packed,
    ULONG PackedSize,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize)
{
    SYMZ_ARENA Arena;
    ELzmaStatus Status;
    SizeT DestLength = UnpackedSize;
    SizeT SourceLength = PackedSize;
    SRes Result;

    Arena.Alloc.Alloc = SymzArenaAlloc;
    Arena.Alloc.Free = SymzArenaFree;
    Arena.Base = Work;
    Arena.Size = WorkSize;
    Arena.Used = 0;

    Result = LzmaDecode(Unpacked,
                        &DestLength,
                        Packed,
                        &SourceLength,
                        Header->LzmaProperties,
                        SYMZ_LZMA_PROPS_SIZE,
                        LZMA_FINISH_ANY,
                        &Status,
                        &Arena.Alloc);

    return Result == SZ_OK && DestLength == UnpackedSize;
}

static
BOOLEAN
SymzReadNumber(
    const UCHAR **Cursor,
    const UCHAR *End,
    PULONG Number)
{
    const UCHAR *Position = *Cursor;
    ULONG Result = 0;
    ULONG Shift = 0;
    UCHAR Value;

    do
    {
        if (Position >= End || Shift > 28)
            return FALSE;

        Value = *Position++;
        Result |= (ULONG)(Value & 0x7F) << Shift;
        Shift += 7;
    } while (Value & 0x80);

    *Number = Result;
    *Cursor = Position;
    return TRUE;
}

static
BOOLEAN
SymzNextLine(
    const UCHAR **Cursor,
    const UCHAR *End,
    PULONG Rva,
    PULONG File,
    PULONG Line)
{
    ULONG Delta;
    ULONG Value;

    if (!SymzReadNumber(Cursor, End, &Delta) || !SymzReadNumber(Cursor, End, &Value))
        return FALSE;

    *Rva += Delta;
    if (Value & 2)
        *Line -= Value >> 2;
    else
        *Line += Value >> 2;

    if (Value & 1)
        return SymzReadNumber(Cursor, End, File);

    return TRUE;
}

static
BOOLEAN
SymzNextEntry(
    const UCHAR **Cursor,
    const UCHAR *End,
    PULONG Rva,
    const char **Name)
{
    const UCHAR *Position;
    ULONG Delta;

    if (!SymzReadNumber(Cursor, End, &Delta))
        return FALSE;

    Position = *Cursor;
    *Name = (const char *)Position;
    while (Position < End && *Position != 0)
        Position++;

    if (Position >= End)
        return FALSE;

    *Rva += Delta;
    *Cursor = Position + 1;
    return TRUE;
}

BOOLEAN
SymzSearchBlock(
    const VOID *Unpacked,
    ULONG UnpackedSize,
    ULONG FirstRva,
    ULONG Rva,
    const char **Name,
    PULONG SymbolRva)
{
    const UCHAR *Cursor = Unpacked;
    const UCHAR *End = Cursor + UnpackedSize;
    const char *EntryName;
    ULONG EntryRva = FirstRva;
    BOOLEAN Found = FALSE;

    while (Cursor < End && SymzNextEntry(&Cursor, End, &EntryRva, &EntryName))
    {
        if (EntryRva > Rva)
            break;

        *Name = EntryName;
        *SymbolRva = EntryRva;
        Found = TRUE;
    }

    return Found;
}

BOOLEAN
SymzEnumerateBlock(
    const VOID *Unpacked,
    ULONG UnpackedSize,
    ULONG FirstRva,
    PSYMZ_ENUM_ROUTINE Routine,
    PVOID Context)
{
    const UCHAR *Cursor = Unpacked;
    const UCHAR *End = Cursor + UnpackedSize;
    const char *Name;
    ULONG Rva = FirstRva;

    while (Cursor < End && SymzNextEntry(&Cursor, End, &Rva, &Name))
    {
        if (!Routine(Context, Rva, Name))
            return FALSE;
    }

    return TRUE;
}

static
const SYMZ_BLOCK *
SymzGetBlocks(
    const SYMZ_HEADER *Header)
{
    return (const SYMZ_BLOCK *)((const UCHAR *)Header + Header->HeaderSize);
}

static
BOOLEAN
SymzLoadBlock(
    const SYMZ_HEADER *Header,
    ULONG TableSize,
    const SYMZ_BLOCK *Block,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize)
{
    if (Block->UnpackedSize > UnpackedSize ||
        Block->DataOffset > TableSize ||
        Block->PackedSize > TableSize - Block->DataOffset)
    {
        return FALSE;
    }

    return SymzDecodeBlock(Header,
                           (const UCHAR *)Header + Block->DataOffset,
                           Block->PackedSize,
                           Unpacked,
                           Block->UnpackedSize,
                           Work,
                           WorkSize);
}

static
const SYMZ_BLOCK *
SymzFindBlock(
    const SYMZ_BLOCK *Blocks,
    ULONG Count,
    ULONG Key)
{
    ULONG Low = 0;
    ULONG High = Count;

    if (Count == 0 || Key < Blocks[0].First)
        return NULL;

    while (High - Low > 1)
    {
        ULONG Middle = Low + (High - Low) / 2;

        if (Blocks[Middle].First <= Key)
            Low = Middle;
        else
            High = Middle;
    }

    return &Blocks[Low];
}

BOOLEAN
SymzSearchLineBlock(
    const VOID *Unpacked,
    ULONG UnpackedSize,
    ULONG FirstRva,
    ULONG Rva,
    PULONG File,
    PULONG Line)
{
    const UCHAR *Cursor = Unpacked;
    const UCHAR *End = Cursor + UnpackedSize;
    ULONG EntryRva = FirstRva;
    ULONG EntryFile = 0;
    ULONG EntryLine = 0;
    BOOLEAN Found = FALSE;

    while (Cursor < End && SymzNextLine(&Cursor, End, &EntryRva, &EntryFile, &EntryLine))
    {
        if (EntryRva > Rva)
            break;

        *File = EntryFile;
        *Line = EntryLine;
        Found = TRUE;
    }

    return Found && *Line != 0 && *File != 0;
}

BOOLEAN
SymzSearchFileBlock(
    const VOID *Unpacked,
    ULONG UnpackedSize,
    ULONG FirstFile,
    ULONG File,
    const char **Name)
{
    const UCHAR *Cursor = Unpacked;
    const UCHAR *End = Cursor + UnpackedSize;
    ULONG Index = FirstFile;

    while (Cursor < End)
    {
        const UCHAR *Start = Cursor;

        while (Cursor < End && *Cursor != 0)
            Cursor++;
        if (Cursor >= End)
            return FALSE;

        if (Index == File)
        {
            *Name = (const char *)Start;
            return TRUE;
        }

        Cursor++;
        Index++;
    }

    return FALSE;
}

BOOLEAN
SymzLookup(
    const VOID *Table,
    ULONG TableSize,
    ULONG Rva,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize,
    const char **Name,
    PULONG SymbolRva)
{
    const SYMZ_HEADER *Header = Table;
    const SYMZ_BLOCK *Block;

    if (!SymzCheckHeader(Header, TableSize))
        return FALSE;

    Block = SymzFindBlock(SymzGetBlocks(Header), Header->SymbolBlockCount, Rva);
    if (!Block || !SymzLoadBlock(Header, TableSize, Block, Unpacked, UnpackedSize, Work, WorkSize))
        return FALSE;

    return SymzSearchBlock(Unpacked, Block->UnpackedSize, Block->First, Rva, Name, SymbolRva);
}

BOOLEAN
SymzLookupLine(
    const VOID *Table,
    ULONG TableSize,
    ULONG Rva,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize,
    const char **FileName,
    PULONG Line)
{
    const SYMZ_HEADER *Header = Table;
    const SYMZ_BLOCK *Blocks;
    const SYMZ_BLOCK *Block;
    ULONG File;

    if (!SymzCheckHeader(Header, TableSize))
        return FALSE;

    Blocks = SymzGetBlocks(Header);
    Block = SymzFindBlock(Blocks + Header->SymbolBlockCount, Header->LineBlockCount, Rva);
    if (!Block ||
        !SymzLoadBlock(Header, TableSize, Block, Unpacked, UnpackedSize, Work, WorkSize) ||
        !SymzSearchLineBlock(Unpacked, Block->UnpackedSize, Block->First, Rva, &File, Line))
    {
        return FALSE;
    }

    Block = SymzFindBlock(Blocks + Header->SymbolBlockCount + Header->LineBlockCount, Header->FileBlockCount, File);
    if (!Block || !SymzLoadBlock(Header, TableSize, Block, Unpacked, UnpackedSize, Work, WorkSize))
        return FALSE;

    return SymzSearchFileBlock(Unpacked, Block->UnpackedSize, Block->First, File, FileName);
}

BOOLEAN
SymzEnumerate(
    const VOID *Table,
    ULONG TableSize,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize,
    PSYMZ_ENUM_ROUTINE Routine,
    PVOID Context)
{
    const SYMZ_HEADER *Header = Table;
    const SYMZ_BLOCK *Blocks;
    ULONG Index;

    if (!SymzCheckHeader(Header, TableSize))
        return FALSE;

    Blocks = SymzGetBlocks(Header);
    for (Index = 0; Index < Header->SymbolBlockCount; Index++)
    {
        if (!SymzLoadBlock(Header, TableSize, &Blocks[Index], Unpacked, UnpackedSize, Work, WorkSize))
            return FALSE;

        if (!SymzEnumerateBlock(Unpacked, Blocks[Index].UnpackedSize, Blocks[Index].First, Routine, Context))
            return TRUE;
    }

    return TRUE;
}

BOOLEAN
SymzEnumerateFiles(
    const VOID *Table,
    ULONG TableSize,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize,
    PSYMZ_ENUM_ROUTINE Routine,
    PVOID Context)
{
    const SYMZ_HEADER *Header = Table;
    const SYMZ_BLOCK *Blocks;
    ULONG Index;

    if (!SymzCheckHeader(Header, TableSize))
        return FALSE;

    Blocks = SymzGetBlocks(Header) + Header->SymbolBlockCount + Header->LineBlockCount;
    for (Index = 0; Index < Header->FileBlockCount; Index++)
    {
        const UCHAR *Cursor = Unpacked;
        const UCHAR *End = Cursor + Blocks[Index].UnpackedSize;
        ULONG File = Blocks[Index].First;

        if (!SymzLoadBlock(Header, TableSize, &Blocks[Index], Unpacked, UnpackedSize, Work, WorkSize))
            return FALSE;

        while (Cursor < End)
        {
            const UCHAR *Start = Cursor;

            while (Cursor < End && *Cursor != 0)
                Cursor++;
            if (Cursor >= End)
                return FALSE;
            if (!Routine(Context, File, (const char *)Start))
                return TRUE;
            Cursor++;
            File++;
        }
    }

    return TRUE;
}

BOOLEAN
SymzEnumerateLines(
    const VOID *Table,
    ULONG TableSize,
    PVOID Unpacked,
    ULONG UnpackedSize,
    PVOID Work,
    ULONG WorkSize,
    PSYMZ_LINE_ROUTINE Routine,
    PVOID Context)
{
    const SYMZ_HEADER *Header = Table;
    const SYMZ_BLOCK *Blocks;
    ULONG Index;

    if (!SymzCheckHeader(Header, TableSize))
        return FALSE;

    Blocks = SymzGetBlocks(Header) + Header->SymbolBlockCount;
    for (Index = 0; Index < Header->LineBlockCount; Index++)
    {
        const UCHAR *Cursor = Unpacked;
        const UCHAR *End = Cursor + Blocks[Index].UnpackedSize;
        ULONG Rva = Blocks[Index].First;
        ULONG File = 0;
        ULONG Line = 0;

        if (!SymzLoadBlock(Header, TableSize, &Blocks[Index], Unpacked, UnpackedSize, Work, WorkSize))
            return FALSE;

        while (Cursor < End && SymzNextLine(&Cursor, End, &Rva, &File, &Line))
        {
            if (!Routine(Context, Rva, File, Line))
                return TRUE;
        }
    }

    return TRUE;
}
