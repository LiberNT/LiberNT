/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Function names from the compressed .symz section of loaded images
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "kdb.h"

#define NDEBUG
#include "debug.h"

#define KDB_SYMZ_MAX_SECTIONS 96

static UCHAR KdbpSymzPacked[SYMZ_MAX_BLOCK_SIZE];
static UCHAR KdbpSymzUnpacked[SYMZ_MAX_BLOCK_SIZE];
static UCHAR KdbpSymzWork[SYMZ_DECODER_WORK_SIZE];
static LONG KdbpSymzBusy;

static
BOOLEAN
KdbpSymzFindTable(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _Out_ PUCHAR *Table,
    _Out_ PULONG TableSize)
{
    IMAGE_DOS_HEADER DosHeader;
    IMAGE_FILE_HEADER FileHeader;
    IMAGE_SECTION_HEADER Section;
    PUCHAR Base = LdrEntry->DllBase;
    PUCHAR SectionAddress;
    ULONG Index;

    if (!NT_SUCCESS(KdbpSafeReadMemory(&DosHeader, Base, sizeof(DosHeader))) ||
        DosHeader.e_magic != IMAGE_DOS_SIGNATURE ||
        DosHeader.e_lfanew <= 0 ||
        (ULONG)DosHeader.e_lfanew >= LdrEntry->SizeOfImage ||
        !NT_SUCCESS(KdbpSafeReadMemory(&FileHeader,
                                       Base + DosHeader.e_lfanew + sizeof(ULONG),
                                       sizeof(FileHeader))) ||
        FileHeader.NumberOfSections > KDB_SYMZ_MAX_SECTIONS)
    {
        return FALSE;
    }

    SectionAddress = Base + DosHeader.e_lfanew + sizeof(ULONG) + sizeof(FileHeader) + FileHeader.SizeOfOptionalHeader;
    for (Index = 0; Index < FileHeader.NumberOfSections; Index++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Section, SectionAddress + Index * sizeof(Section), sizeof(Section))))
            return FALSE;

        if (RtlCompareMemory(Section.Name, SYMZ_SECTION_NAME, sizeof(SYMZ_SECTION_NAME)) == sizeof(SYMZ_SECTION_NAME))
        {
            if (Section.VirtualAddress >= LdrEntry->SizeOfImage ||
                Section.Misc.VirtualSize > LdrEntry->SizeOfImage - Section.VirtualAddress)
            {
                return FALSE;
            }

            *Table = Base + Section.VirtualAddress;
            *TableSize = Section.Misc.VirtualSize;
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
KdbpSymzLoadBlock(
    _In_ PUCHAR Table,
    _In_ ULONG TableSize,
    _In_ const SYMZ_HEADER *Header,
    _In_ ULONG FirstBlock,
    _In_ ULONG BlockCount,
    _In_ ULONG Key,
    _Out_ PSYMZ_BLOCK Block)
{
    SYMZ_BLOCK Candidate;
    PUCHAR Blocks = Table + Header->HeaderSize + FirstBlock * sizeof(SYMZ_BLOCK);
    ULONG Low = 0;
    ULONG High = BlockCount;

    if (BlockCount == 0 ||
        !NT_SUCCESS(KdbpSafeReadMemory(Block, Blocks, sizeof(*Block))) ||
        Key < Block->First)
    {
        return FALSE;
    }

    while (High - Low > 1)
    {
        ULONG Middle = Low + (High - Low) / 2;

        if (!NT_SUCCESS(KdbpSafeReadMemory(&Candidate, Blocks + Middle * sizeof(Candidate), sizeof(Candidate))))
            return FALSE;

        if (Candidate.First <= Key)
        {
            Low = Middle;
            *Block = Candidate;
        }
        else
        {
            High = Middle;
        }
    }

    if (Block->PackedSize > sizeof(KdbpSymzPacked) ||
        Block->UnpackedSize > sizeof(KdbpSymzUnpacked) ||
        Block->DataOffset > TableSize ||
        Block->PackedSize > TableSize - Block->DataOffset)
    {
        return FALSE;
    }

    return NT_SUCCESS(KdbpSafeReadMemory(KdbpSymzPacked, Table + Block->DataOffset, Block->PackedSize)) &&
           SymzDecodeBlock(Header,
                           KdbpSymzPacked,
                           Block->PackedSize,
                           KdbpSymzUnpacked,
                           Block->UnpackedSize,
                           KdbpSymzWork,
                           sizeof(KdbpSymzWork));
}

BOOLEAN
KdbpSymzDescribe(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ ULONG_PTR RelativeAddress,
    _Out_writes_z_(FunctionNameLength) PCHAR FunctionName,
    _In_ ULONG FunctionNameLength,
    _Out_ PULONG_PTR SymbolAddress,
    _Out_writes_opt_z_(FileNameLength) PCHAR FileName,
    _In_ ULONG FileNameLength,
    _Out_opt_ PULONG Line)
{
    SYMZ_HEADER Header;
    SYMZ_BLOCK Block;
    PUCHAR Table;
    ULONG TableSize;
    ULONG SymbolRva;
    ULONG File;
    ULONG LineNumber;
    const char *Name;
    BOOLEAN Found = FALSE;

    if (FunctionNameLength == 0 || RelativeAddress > MAXULONG)
        return FALSE;

    FunctionName[0] = ANSI_NULL;
    if (FileName && FileNameLength != 0)
        FileName[0] = ANSI_NULL;
    if (Line)
        *Line = 0;

    if (!KdbpSymzFindTable(LdrEntry, &Table, &TableSize) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&Header, Table, sizeof(Header))) ||
        !SymzCheckHeader(&Header, TableSize))
    {
        return FALSE;
    }

    if (InterlockedCompareExchange(&KdbpSymzBusy, 1, 0) != 0)
        return FALSE;

    if (KdbpSymzLoadBlock(Table, TableSize, &Header, 0, Header.SymbolBlockCount, (ULONG)RelativeAddress, &Block) &&
        SymzSearchBlock(KdbpSymzUnpacked, Block.UnpackedSize, Block.First, (ULONG)RelativeAddress, &Name, &SymbolRva))
    {
        RtlStringCbCopyA(FunctionName, FunctionNameLength, Name);
        *SymbolAddress = SymbolRva;
        Found = TRUE;
    }

    if (Found && FileName && FileNameLength != 0 && Line &&
        KdbpSymzLoadBlock(Table, TableSize, &Header, Header.SymbolBlockCount, Header.LineBlockCount, (ULONG)RelativeAddress, &Block) &&
        SymzSearchLineBlock(KdbpSymzUnpacked, Block.UnpackedSize, Block.First, (ULONG)RelativeAddress, &File, &LineNumber) &&
        KdbpSymzLoadBlock(Table, TableSize, &Header, Header.SymbolBlockCount + Header.LineBlockCount, Header.FileBlockCount, File, &Block) &&
        SymzSearchFileBlock(KdbpSymzUnpacked, Block.UnpackedSize, Block.First, File, &Name))
    {
        RtlStringCbCopyA(FileName, FileNameLength, Name);
        *Line = LineNumber;
    }

    InterlockedExchange(&KdbpSymzBusy, 0);
    return Found;
}

BOOLEAN
KdbpSymzEnumerate(
    _In_ PLDR_DATA_TABLE_ENTRY LdrEntry,
    _In_ PSYMZ_ENUM_ROUTINE Routine,
    _In_ PVOID Context)
{
    SYMZ_HEADER Header;
    SYMZ_BLOCK Block;
    PUCHAR Table;
    ULONG TableSize;
    ULONG Index;
    BOOLEAN Complete = TRUE;

    if (!KdbpSymzFindTable(LdrEntry, &Table, &TableSize) ||
        !NT_SUCCESS(KdbpSafeReadMemory(&Header, Table, sizeof(Header))) ||
        !SymzCheckHeader(&Header, TableSize))
    {
        return TRUE;
    }

    if (InterlockedCompareExchange(&KdbpSymzBusy, 1, 0) != 0)
        return FALSE;

    for (Index = 0; Index < Header.SymbolBlockCount && Complete; Index++)
    {
        if (!NT_SUCCESS(KdbpSafeReadMemory(&Block,
                                           Table + Header.HeaderSize + Index * sizeof(Block),
                                           sizeof(Block))) ||
            !KdbpSymzLoadBlock(Table, TableSize, &Header, Index, 1, Block.First, &Block))
        {
            Complete = FALSE;
            break;
        }

        Complete = SymzEnumerateBlock(KdbpSymzUnpacked, Block.UnpackedSize, Block.First, Routine, Context);
    }

    InterlockedExchange(&KdbpSymzBusy, 0);
    return Complete;
}

VOID
KdbSymzPrepareImage(
    _In_ PVOID ImageBase)
{
    PUCHAR Base = ImageBase;
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER Section;
    volatile UCHAR Byte;
    ULONG Index;
    ULONG Offset;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL || (ULONG_PTR)ImageBase > (ULONG_PTR)MmHighestUserAddress)
        return;

    _SEH2_TRY
    {
        NtHeaders = RtlImageNtHeader(Base);
        if (NtHeaders && NtHeaders->FileHeader.NumberOfSections <= KDB_SYMZ_MAX_SECTIONS)
        {
            Section = IMAGE_FIRST_SECTION(NtHeaders);
            for (Index = 0; Index < NtHeaders->FileHeader.NumberOfSections; Index++)
            {
                if (RtlCompareMemory(Section[Index].Name, SYMZ_SECTION_NAME, sizeof(SYMZ_SECTION_NAME)) != sizeof(SYMZ_SECTION_NAME))
                    continue;

                if (Section[Index].VirtualAddress < NtHeaders->OptionalHeader.SizeOfImage &&
                    Section[Index].Misc.VirtualSize <= NtHeaders->OptionalHeader.SizeOfImage - Section[Index].VirtualAddress)
                {
                    for (Offset = 0; Offset < Section[Index].Misc.VirtualSize; Offset += PAGE_SIZE)
                        Byte = Base[Section[Index].VirtualAddress + Offset];
                }
                break;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    _SEH2_END;

    (VOID)Byte;
}
