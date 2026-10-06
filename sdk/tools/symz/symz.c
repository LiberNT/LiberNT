/*
 * PROJECT:     LiberNT Build Tools
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Replace the COFF symbol table of an image by a compressed .symz section
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <typedefs.h>
#include <pecoff.h>
#include <symz.h>

#include "LzmaEnc.h"

#define COFF_SYMBOL_SIZE 18
#define COFF_CLASS_EXTERNAL 2
#define COFF_CLASS_STATIC 3

typedef struct _SYMBOL
{
    ULONG Rva;
    ULONG Order;
    const char *Name;
    ULONG NameLength;
} SYMBOL;

typedef struct _BUFFER
{
    UCHAR *Data;
    size_t Size;
    size_t Capacity;
} BUFFER;

typedef struct _LINE
{
    ULONG Rva;
    ULONG Order;
    ULONG File;
    ULONG Line;
} LINE;

typedef struct _LINES
{
    LINE *Rows;
    size_t Count;
    size_t Capacity;
    char **Files;
    ULONG *FileHash;
    ULONG FileCount;
    ULONG FileCapacity;
    ULONG HashSize;
} LINES;

typedef struct _READER
{
    const UCHAR *Cursor;
    const UCHAR *End;
    int Failed;
} READER;

static void *
SzAlloc(ISzAllocPtr Alloc, size_t Size)
{
    return malloc(Size);
}

static void
SzFree(ISzAllocPtr Alloc, void *Address)
{
    free(Address);
}

static const ISzAlloc Allocator = { SzAlloc, SzFree };

static int
Fail(const char *File, const char *Message)
{
    fprintf(stderr, "symz: %s: %s\n", File, Message);
    return 1;
}

static void
Append(BUFFER *Buffer, const void *Data, size_t Size)
{
    if (Buffer->Size + Size > Buffer->Capacity)
    {
        Buffer->Capacity = (Buffer->Size + Size) * 2 + 0x1000;
        Buffer->Data = realloc(Buffer->Data, Buffer->Capacity);
        if (!Buffer->Data)
        {
            fprintf(stderr, "symz: out of memory\n");
            exit(1);
        }
    }

    if (Data)
        memcpy(Buffer->Data + Buffer->Size, Data, Size);
    else
        memset(Buffer->Data + Buffer->Size, 0, Size);
    Buffer->Size += Size;
}

static int
CompareSymbols(const void *Left, const void *Right)
{
    const SYMBOL *A = Left;
    const SYMBOL *B = Right;

    if (A->Rva != B->Rva)
        return A->Rva < B->Rva ? -1 : 1;
    if (A->Order != B->Order)
        return A->Order < B->Order ? -1 : 1;
    return 0;
}

static ULONG
AlignUp(ULONG Value, ULONG Alignment)
{
    return (Value + Alignment - 1) / Alignment * Alignment;
}

static ULONG
ImageChecksum(const UCHAR *Data, size_t Size, size_t ChecksumOffset)
{
    ULONG Sum = 0;
    size_t Index;

    for (Index = 0; Index + 1 < Size; Index += 2)
    {
        if (Index == ChecksumOffset || Index == ChecksumOffset + 2)
            continue;
        Sum += Data[Index] | (Data[Index + 1] << 8);
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
    }
    if (Size & 1)
    {
        Sum += Data[Size - 1];
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
    }
    Sum = (Sum & 0xFFFF) + (Sum >> 16);
    return Sum + (ULONG)Size;
}

static void
AppendNumber(BUFFER *Buffer, ULONG Value)
{
    UCHAR Encoded[5];
    ULONG Length = 0;

    do
    {
        Encoded[Length] = Value & 0x7F;
        Value >>= 7;
        if (Value)
            Encoded[Length] |= 0x80;
        Length++;
    } while (Value);

    Append(Buffer, Encoded, Length);
}

static ULONG
ReadBytes(READER *Reader, unsigned Count)
{
    ULONG Value = 0;
    unsigned Index;

    if ((size_t)(Reader->End - Reader->Cursor) < Count)
    {
        Reader->Failed = 1;
        Reader->Cursor = Reader->End;
        return 0;
    }
    for (Index = 0; Index < Count && Index < 4; Index++)
        Value |= (ULONG)Reader->Cursor[Index] << (8 * Index);
    Reader->Cursor += Count;
    return Value;
}

static unsigned long long
ReadAddress(READER *Reader, unsigned Count)
{
    unsigned long long Value = 0;
    unsigned Index;

    if ((size_t)(Reader->End - Reader->Cursor) < Count)
    {
        Reader->Failed = 1;
        Reader->Cursor = Reader->End;
        return 0;
    }
    for (Index = 0; Index < Count && Index < 8; Index++)
        Value |= (unsigned long long)Reader->Cursor[Index] << (8 * Index);
    Reader->Cursor += Count;
    return Value;
}

static unsigned long long
ReadUleb(READER *Reader)
{
    unsigned long long Value = 0;
    unsigned Shift = 0;
    UCHAR Byte;

    do
    {
        if (Reader->Cursor >= Reader->End)
        {
            Reader->Failed = 1;
            return 0;
        }
        Byte = *Reader->Cursor++;
        if (Shift < 64)
            Value |= (unsigned long long)(Byte & 0x7F) << Shift;
        Shift += 7;
    } while (Byte & 0x80);

    return Value;
}

static long long
ReadSleb(READER *Reader)
{
    unsigned long long Value = 0;
    unsigned Shift = 0;
    UCHAR Byte;

    do
    {
        if (Reader->Cursor >= Reader->End)
        {
            Reader->Failed = 1;
            return 0;
        }
        Byte = *Reader->Cursor++;
        if (Shift < 64)
            Value |= (unsigned long long)(Byte & 0x7F) << Shift;
        Shift += 7;
    } while (Byte & 0x80);

    if (Shift < 64 && (Byte & 0x40))
        Value |= ~0ULL << Shift;
    return (long long)Value;
}

static const char *
ReadString(READER *Reader)
{
    const char *String = (const char *)Reader->Cursor;

    while (Reader->Cursor < Reader->End && *Reader->Cursor != 0)
        Reader->Cursor++;
    if (Reader->Cursor >= Reader->End)
    {
        Reader->Failed = 1;
        return "";
    }
    Reader->Cursor++;
    return String;
}

static ULONG
HashString(const char *String)
{
    ULONG Hash = 2166136261u;

    while (*String)
        Hash = (Hash ^ (UCHAR)*String++) * 16777619u;
    return Hash;
}

static ULONG
InternFile(LINES *Lines, const char *Directory, const char *Name)
{
    char Path[SYMZ_MAX_NAME];
    ULONG Slot;
    ULONG Index;

    if (Directory && Directory[0] && Name[0] != '/' && Name[0] != '\\')
        snprintf(Path, sizeof(Path), "%s/%s", Directory, Name);
    else
        snprintf(Path, sizeof(Path), "%s", Name);

    if (Lines->FileCount * 2 >= Lines->HashSize)
    {
        ULONG NewSize = Lines->HashSize ? Lines->HashSize * 2 : 1024;
        ULONG *NewHash = calloc(NewSize, sizeof(ULONG));

        if (!NewHash)
        {
            fprintf(stderr, "symz: out of memory\n");
            exit(1);
        }
        for (Index = 0; Index < Lines->FileCount; Index++)
        {
            Slot = HashString(Lines->Files[Index]) & (NewSize - 1);
            while (NewHash[Slot])
                Slot = (Slot + 1) & (NewSize - 1);
            NewHash[Slot] = Index + 1;
        }
        free(Lines->FileHash);
        Lines->FileHash = NewHash;
        Lines->HashSize = NewSize;
    }

    Slot = HashString(Path) & (Lines->HashSize - 1);
    while (Lines->FileHash[Slot])
    {
        if (strcmp(Lines->Files[Lines->FileHash[Slot] - 1], Path) == 0)
            return Lines->FileHash[Slot];
        Slot = (Slot + 1) & (Lines->HashSize - 1);
    }

    if (Lines->FileCount == Lines->FileCapacity)
    {
        Lines->FileCapacity = Lines->FileCapacity ? Lines->FileCapacity * 2 : 256;
        Lines->Files = realloc(Lines->Files, Lines->FileCapacity * sizeof(char *));
    }
    if (!Lines->Files || !(Lines->Files[Lines->FileCount] = strdup(Path)))
    {
        fprintf(stderr, "symz: out of memory\n");
        exit(1);
    }
    Lines->FileHash[Slot] = ++Lines->FileCount;
    return Lines->FileCount;
}

static void
AddLine(LINES *Lines, ULONG Rva, ULONG File, ULONG Line)
{
    if (Lines->Count == Lines->Capacity)
    {
        Lines->Capacity = Lines->Capacity ? Lines->Capacity * 2 : 4096;
        Lines->Rows = realloc(Lines->Rows, Lines->Capacity * sizeof(LINE));
        if (!Lines->Rows)
        {
            fprintf(stderr, "symz: out of memory\n");
            exit(1);
        }
    }
    Lines->Rows[Lines->Count].Rva = Rva;
    Lines->Rows[Lines->Count].Order = (ULONG)Lines->Count;
    Lines->Rows[Lines->Count].File = File;
    Lines->Rows[Lines->Count].Line = Line;
    Lines->Count++;
}

static int
CompareLines(const void *Left, const void *Right)
{
    const LINE *A = Left;
    const LINE *B = Right;

    if (A->Rva != B->Rva)
        return A->Rva < B->Rva ? -1 : 1;
    if (A->Order != B->Order)
        return A->Order < B->Order ? -1 : 1;
    return 0;
}

static void
DecodeLineUnit(
    LINES *Lines,
    READER *Unit,
    unsigned Version,
    unsigned long long ImageBase,
    ULONG SizeOfImage)
{
    READER Header;
    READER Program;
    const char *Directories[1024];
    ULONG *FileMap = NULL;
    ULONG FileMapCount = 0;
    ULONG FileMapCapacity = 0;
    ULONG DirectoryCount = 0;
    ULONG HeaderLength;
    UCHAR MinimumInstructionLength;
    UCHAR LineRange;
    UCHAR OpcodeBase;
    UCHAR OpcodeLengths[256] = { 0 };
    signed char LineBase;
    unsigned long long Address = 0;
    ULONG File = 1;
    ULONG Line = 1;
    ULONG Index;
    int Valid = 0;

    HeaderLength = ReadBytes(Unit, 4);
    if (Unit->Failed || HeaderLength > (size_t)(Unit->End - Unit->Cursor))
        return;

    Header.Cursor = Unit->Cursor;
    Header.End = Unit->Cursor + HeaderLength;
    Header.Failed = 0;
    Program.Cursor = Header.End;
    Program.End = Unit->End;
    Program.Failed = 0;

    MinimumInstructionLength = (UCHAR)ReadBytes(&Header, 1);
    if (Version >= 4)
        ReadBytes(&Header, 1);
    ReadBytes(&Header, 1);
    LineBase = (signed char)ReadBytes(&Header, 1);
    LineRange = (UCHAR)ReadBytes(&Header, 1);
    OpcodeBase = (UCHAR)ReadBytes(&Header, 1);
    for (Index = 1; Index < OpcodeBase; Index++)
        OpcodeLengths[Index] = (UCHAR)ReadBytes(&Header, 1);
    if (Header.Failed || LineRange == 0)
        return;

    for (;;)
    {
        const char *Directory = ReadString(&Header);

        if (Header.Failed || !Directory[0])
            break;
        if (DirectoryCount < 1023)
            Directories[++DirectoryCount] = Directory;
    }

#define ADD_FILE(Name, DirectoryIndex) \
    do { \
        if (FileMapCount == FileMapCapacity) \
        { \
            FileMapCapacity = FileMapCapacity ? FileMapCapacity * 2 : 64; \
            FileMap = realloc(FileMap, FileMapCapacity * sizeof(ULONG)); \
            if (!FileMap) { fprintf(stderr, "symz: out of memory\n"); exit(1); } \
        } \
        FileMap[FileMapCount++] = InternFile(Lines, \
            ((DirectoryIndex) >= 1 && (DirectoryIndex) <= DirectoryCount) ? Directories[DirectoryIndex] : NULL, Name); \
    } while (0)

    while (!Header.Failed)
    {
        const char *Name = ReadString(&Header);
        ULONG DirectoryIndex;

        if (Header.Failed || !Name[0])
            break;
        DirectoryIndex = (ULONG)ReadUleb(&Header);
        ReadUleb(&Header);
        ReadUleb(&Header);
        ADD_FILE(Name, DirectoryIndex);
    }

#define EMIT(EndSequence) \
    do { \
        if (Valid && Address >= ImageBase && Address - ImageBase < SizeOfImage) \
        { \
            if (EndSequence) \
                AddLine(Lines, (ULONG)(Address - ImageBase), 0, 0); \
            else if (File >= 1 && File <= FileMapCount) \
                AddLine(Lines, (ULONG)(Address - ImageBase), FileMap[File - 1], Line); \
        } \
    } while (0)

    while (Program.Cursor < Program.End && !Program.Failed)
    {
        UCHAR Opcode = (UCHAR)ReadBytes(&Program, 1);

        if (Opcode >= OpcodeBase)
        {
            UCHAR Adjusted = Opcode - OpcodeBase;

            Address += (unsigned long long)(Adjusted / LineRange) * MinimumInstructionLength;
            Line += LineBase + (Adjusted % LineRange);
            EMIT(0);
        }
        else if (Opcode == 0)
        {
            ULONG Length = (ULONG)ReadUleb(&Program);
            const UCHAR *Next;
            UCHAR Extended;

            if (Program.Failed || Length == 0 || Length > (size_t)(Program.End - Program.Cursor))
                break;
            Next = Program.Cursor + Length;
            Extended = (UCHAR)ReadBytes(&Program, 1);
            if (Extended == 1)
            {
                EMIT(1);
                Address = 0;
                File = 1;
                Line = 1;
                Valid = 0;
            }
            else if (Extended == 2)
            {
                Address = ReadAddress(&Program, Length - 1);
                Valid = Address >= ImageBase && Address - ImageBase < SizeOfImage && Address != 0;
            }
            else if (Extended == 3)
            {
                const char *Name = ReadString(&Program);
                ULONG DirectoryIndex = (ULONG)ReadUleb(&Program);

                ADD_FILE(Name, DirectoryIndex);
            }
            Program.Cursor = Next;
        }
        else
        {
            switch (Opcode)
            {
                case 1:
                    EMIT(0);
                    break;
                case 2:
                    Address += ReadUleb(&Program) * MinimumInstructionLength;
                    break;
                case 3:
                    Line += (ULONG)ReadSleb(&Program);
                    break;
                case 4:
                    File = (ULONG)ReadUleb(&Program);
                    break;
                case 8:
                    Address += (unsigned long long)((255 - OpcodeBase) / LineRange) * MinimumInstructionLength;
                    break;
                case 9:
                    Address += ReadBytes(&Program, 2);
                    break;
                default:
                    for (Index = 0; Index < OpcodeLengths[Opcode]; Index++)
                        ReadUleb(&Program);
                    break;
            }
        }
    }

#undef EMIT
#undef ADD_FILE
    free(FileMap);
}

static void
LoadLines(
    LINES *Lines,
    const char *FileName,
    unsigned long long ImageBase,
    ULONG SizeOfImage)
{
    FILE *File = fopen(FileName, "rb");
    UCHAR *Data;
    long Size;
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_FILE_HEADER FileHeader;
    PIMAGE_SECTION_HEADER Sections;
    ULONG StringOffset;
    ULONG Index;
    READER Reader;

    if (!File)
        return;
    fseek(File, 0, SEEK_END);
    Size = ftell(File);
    fseek(File, 0, SEEK_SET);
    Data = malloc(Size > 0 ? Size : 1);
    if (!Data || Size <= 0 || fread(Data, 1, Size, File) != (size_t)Size)
    {
        fclose(File);
        free(Data);
        return;
    }
    fclose(File);

    DosHeader = (PIMAGE_DOS_HEADER)Data;
    if ((size_t)Size < sizeof(IMAGE_DOS_HEADER) ||
        DosHeader->e_magic != 0x5A4D ||
        (size_t)DosHeader->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) > (size_t)Size ||
        memcmp(Data + DosHeader->e_lfanew, "PE\0\0", 4) != 0)
    {
        free(Data);
        return;
    }

    FileHeader = (PIMAGE_FILE_HEADER)(Data + DosHeader->e_lfanew + 4);
    Sections = (PIMAGE_SECTION_HEADER)((UCHAR *)(FileHeader + 1) + FileHeader->SizeOfOptionalHeader);
    if ((UCHAR *)(Sections + FileHeader->NumberOfSections) > Data + Size)
    {
        free(Data);
        return;
    }
    StringOffset = FileHeader->PointerToSymbolTable + FileHeader->NumberOfSymbols * COFF_SYMBOL_SIZE;

    for (Index = 0; Index < FileHeader->NumberOfSections; Index++)
    {
        char Name[64] = { 0 };
        ULONG Length;

        memcpy(Name, Sections[Index].Name, 8);
        if (Name[0] == '/' && FileHeader->PointerToSymbolTable != 0)
        {
            ULONG Offset = (ULONG)strtoul(Name + 1, NULL, 10);

            if (StringOffset + Offset < (ULONG)Size)
                snprintf(Name, sizeof(Name), "%.60s", (const char *)Data + StringOffset + Offset);
        }
        if (strcmp(Name, ".debug_line") != 0)
            continue;

        Length = Sections[Index].Misc.VirtualSize;
        if (Length == 0 || Length > Sections[Index].SizeOfRawData)
            Length = Sections[Index].SizeOfRawData;
        if (Sections[Index].PointerToRawData > (ULONG)Size ||
            Length > (ULONG)Size - Sections[Index].PointerToRawData)
        {
            break;
        }

        Reader.Cursor = Data + Sections[Index].PointerToRawData;
        Reader.End = Reader.Cursor + Length;
        Reader.Failed = 0;
        while ((size_t)(Reader.End - Reader.Cursor) > 6)
        {
            READER Unit;
            ULONG UnitLength = ReadBytes(&Reader, 4);
            unsigned Version;

            if (UnitLength == 0xFFFFFFFF || UnitLength < 2 ||
                UnitLength > (size_t)(Reader.End - Reader.Cursor))
            {
                break;
            }
            Unit.Cursor = Reader.Cursor;
            Unit.End = Reader.Cursor + UnitLength;
            Unit.Failed = 0;
            Reader.Cursor = Unit.End;

            Version = ReadBytes(&Unit, 2);
            if (Version >= 2 && Version <= 4)
                DecodeLineUnit(Lines, &Unit, Version, ImageBase, SizeOfImage);
        }
        break;
    }

    free(Data);
}

static void
FlushBlock(
    BUFFER *Table,
    BUFFER *Packed,
    BUFFER *Block,
    ULONG FirstRva,
    ULONG *BlockCount,
    ULONG *MaxUnpackedSize,
    UCHAR *Properties)
{
    CLzmaEncProps EncoderProperties;
    SYMZ_BLOCK Entry;
    SizeT PackedSize = Block->Size + Block->Size / 3 + 0x400;
    SizeT PropertiesSize = SYMZ_LZMA_PROPS_SIZE;
    UCHAR *Output = malloc(PackedSize);

    LzmaEncProps_Init(&EncoderProperties);
    EncoderProperties.level = 9;
    EncoderProperties.dictSize = 1 << 17;
    EncoderProperties.lc = 3;
    EncoderProperties.lp = 0;
    EncoderProperties.pb = 0;
    EncoderProperties.numThreads = 1;

    if (!Output ||
        LzmaEncode(Output, &PackedSize, Block->Data, Block->Size, &EncoderProperties,
                   Properties, &PropertiesSize, 0, NULL, &Allocator, &Allocator) != SZ_OK ||
        PropertiesSize != SYMZ_LZMA_PROPS_SIZE)
    {
        fprintf(stderr, "symz: compression failed\n");
        exit(1);
    }

    Entry.First = FirstRva;
    Entry.DataOffset = (ULONG)Packed->Size;
    Entry.PackedSize = (ULONG)PackedSize;
    Entry.UnpackedSize = (ULONG)Block->Size;
    Append(Table, &Entry, sizeof(Entry));
    Append(Packed, Output, PackedSize);
    free(Output);

    if (Block->Size > *MaxUnpackedSize)
        *MaxUnpackedSize = (ULONG)Block->Size;
    (*BlockCount)++;
    Block->Size = 0;
}

int
main(int argc, char **argv)
{
    FILE *File;
    UCHAR *Image;
    long FileSize;
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_FILE_HEADER FileHeader;
    PIMAGE_SECTION_HEADER Sections;
    PIMAGE_SECTION_HEADER NewSection;
    UCHAR *OptionalHeader;
    ULONG *SizeOfImage;
    ULONG *SizeOfHeaders;
    ULONG *CheckSum;
    ULONG SectionAlignment;
    ULONG FileAlignment;
    ULONG SymbolOffset;
    ULONG SymbolCount;
    ULONG StringOffset;
    ULONG StringSize;
    ULONG Index;
    ULONG Count = 0;
    ULONG Kept = 0;
    ULONG RawEnd = 0;
    ULONG VirtualEnd = 0;
    ULONG BlockCount = 0;
    ULONG LineBlockCount = 0;
    ULONG FileBlockCount = 0;
    ULONG LineKept = 0;
    ULONG PreviousFile = 0;
    ULONG PreviousLine = 0;
    ULONG BlockFile = 0;
    ULONG BlockLine = 0;
    LINES Lines = { 0 };
    ULONG MaxUnpackedSize = 0;
    ULONG FirstRva = 0;
    ULONG PreviousRva = 0;
    ULONG RawSize;
    SYMBOL *Symbols;
    SYMZ_HEADER Header;
    SYMZ_BLOCK *Blocks;
    BUFFER Table = { 0 };
    BUFFER Packed = { 0 };
    BUFFER Block = { 0 };
    BUFFER Output = { 0 };
    UCHAR Properties[SYMZ_LZMA_PROPS_SIZE] = { 0 };
    char *TempName;

    if (argc != 2 && argc != 3)
    {
        fprintf(stderr, "Usage: symz <image> [<image with debug information>]\n");
        return 1;
    }

    File = fopen(argv[1], "rb");
    if (!File)
        return Fail(argv[1], "cannot open");
    fseek(File, 0, SEEK_END);
    FileSize = ftell(File);
    fseek(File, 0, SEEK_SET);
    Image = malloc(FileSize > 0 ? FileSize : 1);
    if (!Image || FileSize <= 0 || fread(Image, 1, FileSize, File) != (size_t)FileSize)
    {
        fclose(File);
        return Fail(argv[1], "cannot read");
    }
    fclose(File);

    DosHeader = (PIMAGE_DOS_HEADER)Image;
    if ((size_t)FileSize < sizeof(IMAGE_DOS_HEADER) ||
        DosHeader->e_magic != 0x5A4D ||
        (size_t)DosHeader->e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) > (size_t)FileSize ||
        memcmp(Image + DosHeader->e_lfanew, "PE\0\0", 4) != 0)
    {
        return 0;
    }

    FileHeader = (PIMAGE_FILE_HEADER)(Image + DosHeader->e_lfanew + 4);
    OptionalHeader = (UCHAR *)(FileHeader + 1);
    Sections = (PIMAGE_SECTION_HEADER)(OptionalHeader + FileHeader->SizeOfOptionalHeader);
    SymbolOffset = FileHeader->PointerToSymbolTable;
    SymbolCount = FileHeader->NumberOfSymbols;

    if ((UCHAR *)(Sections + FileHeader->NumberOfSections) > Image + FileSize ||
        FileHeader->SizeOfOptionalHeader < 96)
    {
        return Fail(argv[1], "truncated headers");
    }
    if (SymbolOffset == 0 || SymbolCount == 0)
    {
        SymbolOffset = (ULONG)FileSize;
        SymbolCount = 0;
        StringOffset = 0;
        StringSize = 0;
    }
    else
    {
        StringOffset = SymbolOffset + SymbolCount * COFF_SYMBOL_SIZE;
        if (SymbolOffset > (ULONG)FileSize ||
            SymbolCount > ((ULONG)FileSize - SymbolOffset) / COFF_SYMBOL_SIZE ||
            StringOffset + 4 > (ULONG)FileSize)
        {
            return Fail(argv[1], "symbol table outside the file");
        }
        memcpy(&StringSize, Image + StringOffset, 4);
        if (StringSize > (ULONG)FileSize - StringOffset)
            return Fail(argv[1], "string table outside the file");
    }

    SectionAlignment = *(ULONG *)(OptionalHeader + 32);
    FileAlignment = *(ULONG *)(OptionalHeader + 36);
    SizeOfImage = (ULONG *)(OptionalHeader + 56);
    SizeOfHeaders = (ULONG *)(OptionalHeader + 60);
    CheckSum = (ULONG *)(OptionalHeader + 64);
    if (SectionAlignment == 0 || FileAlignment == 0)
        return Fail(argv[1], "invalid alignment");

    for (Index = 0; Index < FileHeader->NumberOfSections; Index++)
    {
        ULONG End;

        if (Sections[Index].Name[0] == '/' ||
            memcmp(Sections[Index].Name, SYMZ_SECTION_NAME, sizeof(SYMZ_SECTION_NAME)) == 0)
        {
            return 0;
        }

        End = Sections[Index].PointerToRawData + Sections[Index].SizeOfRawData;
        if (Sections[Index].SizeOfRawData != 0 && End > RawEnd)
            RawEnd = End;
        End = Sections[Index].VirtualAddress +
              (Sections[Index].Misc.VirtualSize ? Sections[Index].Misc.VirtualSize : Sections[Index].SizeOfRawData);
        if (End > VirtualEnd)
            VirtualEnd = End;
    }

    if (RawEnd > SymbolOffset ||
        (UCHAR *)(Sections + FileHeader->NumberOfSections + 1) > Image + *SizeOfHeaders)
    {
        return 0;
    }

    if (argc == 3)
    {
        unsigned long long ImageBase;

        if (*(USHORT *)OptionalHeader == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            ImageBase = *(ULONG *)(OptionalHeader + 28);
        else
            memcpy(&ImageBase, OptionalHeader + 24, 8);
        LoadLines(&Lines, argv[2], ImageBase, *SizeOfImage);
    }
    if (SymbolCount == 0 && Lines.Count == 0)
        return 0;

    Symbols = malloc((SymbolCount ? SymbolCount : 1) * sizeof(SYMBOL));
    if (!Symbols)
        return Fail(argv[1], "out of memory");

    for (Index = 0; Index < SymbolCount; Index++)
    {
        const UCHAR *Entry = Image + SymbolOffset + Index * COFF_SYMBOL_SIZE;
        ULONG Value;
        short Section;
        UCHAR Class = Entry[16];
        UCHAR Auxiliary = Entry[17];
        const char *Name;
        ULONG NameLength;

        memcpy(&Value, Entry + 8, 4);
        memcpy(&Section, Entry + 12, 2);

        if (Entry[0] == 0 && Entry[1] == 0 && Entry[2] == 0 && Entry[3] == 0)
        {
            ULONG NameOffset;

            memcpy(&NameOffset, Entry + 4, 4);
            if (NameOffset >= StringSize)
                return Fail(argv[1], "symbol name outside the string table");
            Name = (const char *)Image + StringOffset + NameOffset;
            NameLength = (ULONG)strnlen(Name, StringSize - NameOffset);
        }
        else
        {
            Name = (const char *)Entry;
            NameLength = (ULONG)strnlen(Name, 8);
        }

        if (Section > 0 && Section <= FileHeader->NumberOfSections &&
            (Class == COFF_CLASS_EXTERNAL || Class == COFF_CLASS_STATIC) &&
            NameLength != 0 && NameLength < SYMZ_MAX_NAME && Name[0] != '.')
        {
            Symbols[Count].Rva = Sections[Section - 1].VirtualAddress + Value;
            Symbols[Count].Order = Count;
            Symbols[Count].Name = Name;
            Symbols[Count].NameLength = NameLength;
            Count++;
        }

        Index += Auxiliary;
    }

    qsort(Symbols, Count, sizeof(SYMBOL), CompareSymbols);

    for (Index = 0; Index < Count; Index++)
    {
        if (Index != 0 &&
            Symbols[Index].Rva == Symbols[Index - 1].Rva &&
            Symbols[Index].NameLength == Symbols[Index - 1].NameLength &&
            memcmp(Symbols[Index].Name, Symbols[Index - 1].Name, Symbols[Index].NameLength) == 0)
        {
            continue;
        }

        if (Block.Size != 0 && Block.Size + 5 + Symbols[Index].NameLength + 1 > SYMZ_BLOCK_SIZE)
            FlushBlock(&Table, &Packed, &Block, FirstRva, &BlockCount, &MaxUnpackedSize, Properties);

        if (Block.Size == 0)
        {
            FirstRva = Symbols[Index].Rva;
            PreviousRva = FirstRva;
        }

        AppendNumber(&Block, Symbols[Index].Rva - PreviousRva);
        PreviousRva = Symbols[Index].Rva;
        Append(&Block, Symbols[Index].Name, Symbols[Index].NameLength);
        Append(&Block, NULL, 1);
        Kept++;
    }
    if (Block.Size != 0)
        FlushBlock(&Table, &Packed, &Block, FirstRva, &BlockCount, &MaxUnpackedSize, Properties);

    qsort(Lines.Rows, Lines.Count, sizeof(LINE), CompareLines);
    for (Index = 0; Index < Lines.Count; Index++)
    {
        const LINE *Row = &Lines.Rows[Index];
        ULONG Value;

        if (Index + 1 < Lines.Count && Lines.Rows[Index + 1].Rva == Row->Rva)
            continue;
        if (LineKept != 0 && Row->File == PreviousFile && Row->Line == PreviousLine)
            continue;
        if (LineKept == 0 && Row->Line == 0)
            continue;

        if (Block.Size != 0 && Block.Size + 15 > SYMZ_BLOCK_SIZE)
            FlushBlock(&Table, &Packed, &Block, FirstRva, &LineBlockCount, &MaxUnpackedSize, Properties);

        if (Block.Size == 0)
        {
            FirstRva = Row->Rva;
            PreviousRva = FirstRva;
            BlockFile = 0;
            BlockLine = 0;
        }

        AppendNumber(&Block, Row->Rva - PreviousRva);
        if (Row->Line >= BlockLine)
            Value = (Row->Line - BlockLine) << 2;
        else
            Value = ((BlockLine - Row->Line) << 2) | 2;
        if (Row->File != BlockFile)
            Value |= 1;
        AppendNumber(&Block, Value);
        if (Row->File != BlockFile)
            AppendNumber(&Block, Row->File);

        PreviousRva = Row->Rva;
        PreviousFile = BlockFile = Row->File;
        PreviousLine = BlockLine = Row->Line;
        LineKept++;
    }
    if (Block.Size != 0)
        FlushBlock(&Table, &Packed, &Block, FirstRva, &LineBlockCount, &MaxUnpackedSize, Properties);

    if (LineKept != 0)
    {
        ULONG FirstFile = 1;

        for (Index = 0; Index < Lines.FileCount; Index++)
        {
            size_t Length = strlen(Lines.Files[Index]);

            if (Block.Size != 0 && Block.Size + Length + 1 > SYMZ_BLOCK_SIZE)
            {
                FlushBlock(&Table, &Packed, &Block, FirstFile, &FileBlockCount, &MaxUnpackedSize, Properties);
                FirstFile = Index + 1;
            }
            Append(&Block, Lines.Files[Index], Length + 1);
        }
        if (Block.Size != 0)
            FlushBlock(&Table, &Packed, &Block, FirstFile, &FileBlockCount, &MaxUnpackedSize, Properties);
    }

    if (BlockCount == 0 && LineBlockCount == 0)
        return 0;

    memset(&Header, 0, sizeof(Header));
    Header.Magic = SYMZ_MAGIC;
    Header.Version = SYMZ_VERSION;
    Header.HeaderSize = sizeof(Header);
    Header.SymbolBlockCount = BlockCount;
    Header.LineBlockCount = LineBlockCount;
    Header.FileBlockCount = FileBlockCount;
    Header.SymbolCount = Kept;
    Header.LineCount = LineKept;
    Header.FileCount = LineKept != 0 ? Lines.FileCount : 0;
    Header.MaxUnpackedSize = MaxUnpackedSize;
    memcpy(Header.LzmaProperties, Properties, SYMZ_LZMA_PROPS_SIZE);

    Blocks = (SYMZ_BLOCK *)Table.Data;
    for (Index = 0; Index < BlockCount + LineBlockCount + FileBlockCount; Index++)
        Blocks[Index].DataOffset += (ULONG)(sizeof(Header) + Table.Size);

    RawEnd = AlignUp(RawEnd, FileAlignment);
    RawSize = AlignUp((ULONG)(sizeof(Header) + Table.Size + Packed.Size), FileAlignment);

    NewSection = Sections + FileHeader->NumberOfSections;
    memset(NewSection, 0, sizeof(*NewSection));
    memcpy(NewSection->Name, SYMZ_SECTION_NAME, sizeof(SYMZ_SECTION_NAME) - 1);
    NewSection->Misc.VirtualSize = (ULONG)(sizeof(Header) + Table.Size + Packed.Size);
    NewSection->VirtualAddress = AlignUp(VirtualEnd, SectionAlignment);
    NewSection->SizeOfRawData = RawSize;
    NewSection->PointerToRawData = RawEnd;
    NewSection->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;

    FileHeader->NumberOfSections++;
    FileHeader->PointerToSymbolTable = 0;
    FileHeader->NumberOfSymbols = 0;
    *SizeOfImage = AlignUp(NewSection->VirtualAddress + NewSection->Misc.VirtualSize, SectionAlignment);

    Append(&Output, Image, SymbolOffset < RawEnd ? SymbolOffset : RawEnd);
    if (Output.Size < RawEnd)
        Append(&Output, NULL, RawEnd - Output.Size);
    Append(&Output, &Header, sizeof(Header));
    Append(&Output, Table.Data, Table.Size);
    Append(&Output, Packed.Data, Packed.Size);
    Append(&Output, NULL, RawEnd + RawSize - Output.Size);

    *CheckSum = 0;
    memcpy(Output.Data, Image, (UCHAR *)(NewSection + 1) - Image);
    Index = ImageChecksum(Output.Data, Output.Size, (UCHAR *)CheckSum - Image);
    memcpy(Output.Data + ((UCHAR *)CheckSum - Image), &Index, 4);

    TempName = malloc(strlen(argv[1]) + 6);
    if (!TempName)
        return Fail(argv[1], "out of memory");
    strcpy(TempName, argv[1]);
    strcat(TempName, ".symz");

    File = fopen(TempName, "wb");
    if (!File || fwrite(Output.Data, 1, Output.Size, File) != Output.Size || fclose(File) != 0)
        return Fail(TempName, "cannot write");

    remove(argv[1]);
    if (rename(TempName, argv[1]) != 0)
        return Fail(argv[1], "cannot replace");

    return 0;
}
