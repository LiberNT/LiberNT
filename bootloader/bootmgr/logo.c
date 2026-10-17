/*
 * PROJECT:     LiberNT Boot Loader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Logo of a detected Windows installation, read from its own boot resources
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <bl.h>
#include <gui.h>
#include <libwim/wimcore.h>

#define BM_LOGO_RESOURCES  L"\\EFI\\Microsoft\\Boot\\Resources\\bootres.dll"
#define BM_LOGO_RESOURCE   1
#define BM_RESOURCE_RCDATA 10
#define BM_LOGO_MAX_SIDE   512

static PCSTR const BmLogoNames[] =
{
    "winlogo1.bmp",
    "winlogo2.bmp",
    "winlogo3.bmp",
    "winlogo4.bmp",
    "winlogo5.bmp",
};

static
ULONG
BmReadLe32(
    _In_reads_bytes_(4) const UCHAR* Data)
{
    return Data[0] | (Data[1] << 8) | (Data[2] << 16) | ((ULONG)Data[3] << 24);
}

static
PUCHAR
BmRvaToData(
    _In_ PUCHAR Image,
    _In_ ULONG Size,
    _In_ PIMAGE_SECTION_HEADER Sections,
    _In_ ULONG SectionCount,
    _In_ ULONG Rva,
    _In_ ULONG Length)
{
    ULONG i;

    for (i = 0; i < SectionCount; i++)
    {
        ULONG Start = Sections[i].VirtualAddress;
        ULONG Span = Sections[i].SizeOfRawData;
        ULONG Offset;

        if (Rva < Start || Rva - Start >= Span || Length > Span - (Rva - Start))
            continue;

        Offset = Sections[i].PointerToRawData + (Rva - Start);
        if (Offset > Size || Length > Size - Offset)
            return NULL;

        return Image + Offset;
    }

    return NULL;
}

static
PIMAGE_RESOURCE_DIRECTORY_ENTRY
BmFindResourceEntry(
    _In_ PUCHAR Root,
    _In_ ULONG RootSize,
    _In_ ULONG DirectoryOffset,
    _In_ BOOLEAN AnyId,
    _In_ USHORT Id)
{
    PIMAGE_RESOURCE_DIRECTORY Directory;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY Entries;
    ULONG Count, i;

    if (DirectoryOffset > RootSize || RootSize - DirectoryOffset < sizeof(*Directory))
        return NULL;

    Directory = (PIMAGE_RESOURCE_DIRECTORY)(Root + DirectoryOffset);
    Count = Directory->NumberOfNamedEntries + Directory->NumberOfIdEntries;
    if (Count > (RootSize - DirectoryOffset - sizeof(*Directory)) / sizeof(*Entries))
        return NULL;

    Entries = (PIMAGE_RESOURCE_DIRECTORY_ENTRY)(Directory + 1);
    for (i = 0; i < Count; i++)
    {
        if (!Entries[i].NameIsString && (AnyId || Entries[i].Id == Id))
            return &Entries[i];
    }

    return NULL;
}

static
PUCHAR
BmFindRcData(
    _In_ PUCHAR Image,
    _In_ ULONG Size,
    _In_ USHORT Name,
    _Out_ PULONG DataSize)
{
    PIMAGE_DOS_HEADER Dos = (PIMAGE_DOS_HEADER)Image;
    PIMAGE_NT_HEADERS32 Nt;
    PIMAGE_DATA_DIRECTORY Directory;
    PIMAGE_SECTION_HEADER Sections;
    PIMAGE_RESOURCE_DIRECTORY_ENTRY Entry;
    PIMAGE_RESOURCE_DATA_ENTRY Data;
    PUCHAR Root;
    ULONG SectionCount, NtOffset;

    if (Size < sizeof(*Dos) || Dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;

    NtOffset = (ULONG)Dos->e_lfanew;
    if (NtOffset > Size || Size - NtOffset < sizeof(IMAGE_NT_HEADERS64))
        return NULL;

    Nt = (PIMAGE_NT_HEADERS32)(Image + NtOffset);
    if (Nt->Signature != IMAGE_NT_SIGNATURE)
        return NULL;

    if (Nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        PIMAGE_NT_HEADERS64 Nt64 = (PIMAGE_NT_HEADERS64)Nt;
        if (Nt64->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_RESOURCE)
            return NULL;
        Directory = &Nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    }
    else if (Nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        if (Nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_RESOURCE)
            return NULL;
        Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    }
    else
    {
        return NULL;
    }

    Sections = IMAGE_FIRST_SECTION(Nt);
    SectionCount = Nt->FileHeader.NumberOfSections;
    if ((PUCHAR)Sections < Image || (PUCHAR)Sections > Image + Size ||
        SectionCount > (ULONG)(Image + Size - (PUCHAR)Sections) / sizeof(*Sections))
    {
        return NULL;
    }

    Root = BmRvaToData(Image, Size, Sections, SectionCount, Directory->VirtualAddress, Directory->Size);
    if (!Root)
        return NULL;

    Entry = BmFindResourceEntry(Root, Directory->Size, 0, FALSE, BM_RESOURCE_RCDATA);
    if (!Entry || !Entry->DataIsDirectory)
        return NULL;
    Entry = BmFindResourceEntry(Root, Directory->Size, Entry->OffsetToDirectory, FALSE, Name);
    if (!Entry || !Entry->DataIsDirectory)
        return NULL;
    Entry = BmFindResourceEntry(Root, Directory->Size, Entry->OffsetToDirectory, TRUE, 0);
    if (!Entry || Entry->DataIsDirectory ||
        Entry->OffsetToData > Directory->Size ||
        Directory->Size - Entry->OffsetToData < sizeof(*Data))
    {
        return NULL;
    }

    Data = (PIMAGE_RESOURCE_DATA_ENTRY)(Root + Entry->OffsetToData);
    *DataSize = Data->Size;
    return BmRvaToData(Image, Size, Sections, SectionCount, Data->OffsetToData, Data->Size);
}

static
BOOLEAN
BmDecodeLogo(
    _In_reads_bytes_(Size) const UCHAR* Bitmap,
    _In_ SIZE_T Size,
    _Out_ PUI_IMAGE Image)
{
    ULONG DataOffset, HeaderSize, Bits, Compression, Stride, X, Y;
    LONG Width, Height;
    BOOLEAN HasAlpha = FALSE;

    if (Size < 54 || Bitmap[0] != 'B' || Bitmap[1] != 'M')
        return FALSE;

    DataOffset = BmReadLe32(Bitmap + 10);
    HeaderSize = BmReadLe32(Bitmap + 14);
    Width = (LONG)BmReadLe32(Bitmap + 18);
    Height = (LONG)BmReadLe32(Bitmap + 22);
    Bits = Bitmap[28] | (Bitmap[29] << 8);
    Compression = BmReadLe32(Bitmap + 30);
    if (HeaderSize < 40 || Compression != 0 || (Bits != 24 && Bits != 32) ||
        Width <= 0 || Width > BM_LOGO_MAX_SIDE ||
        Height == 0 || Height < -BM_LOGO_MAX_SIDE || Height > BM_LOGO_MAX_SIDE)
    {
        return FALSE;
    }

    Stride = ((ULONG)Width * (Bits / 8) + 3) & ~3UL;
    if (DataOffset > Size || (SIZE_T)Stride * (ULONG)abs(Height) > Size - DataOffset)
        return FALSE;

    Image->Width = (ULONG)Width;
    Image->Height = (ULONG)abs(Height);
    Image->Pixels = FwAllocate((SIZE_T)Image->Width * Image->Height * sizeof(ULONG));
    if (!Image->Pixels)
        return FALSE;

    for (Y = 0; Bits == 32 && Y < Image->Height && !HasAlpha; Y++)
    {
        const UCHAR* Row = Bitmap + DataOffset + (SIZE_T)Y * Stride;
        for (X = 0; X < Image->Width; X++)
        {
            if (Row[X * 4 + 3] != 0xFF)
            {
                HasAlpha = TRUE;
                break;
            }
        }
    }

    for (Y = 0; Y < Image->Height; Y++)
    {
        ULONG SourceRow = Height > 0 ? Image->Height - 1 - Y : Y;
        const UCHAR* Row = Bitmap + DataOffset + (SIZE_T)SourceRow * Stride;

        for (X = 0; X < Image->Width; X++)
        {
            const UCHAR* Pixel = Row + X * (Bits / 8);
            ULONG Blue = Pixel[0], Green = Pixel[1], Red = Pixel[2];
            ULONG Alpha = HasAlpha ? Pixel[3] : max(Red, max(Green, Blue));

            if (Alpha && !HasAlpha)
            {
                Red = Red * 255 / Alpha;
                Green = Green * 255 / Alpha;
                Blue = Blue * 255 / Alpha;
            }

            Image->Pixels[Y * Image->Width + X] = (Alpha << 24) | (Red << 16) | (Green << 8) | Blue;
        }
    }

    return TRUE;
}

static
void*
BmWimAllocate(
    size_t Size,
    void* Context)
{
    UNREFERENCED_PARAMETER(Context);
    return FwAllocate(Size);
}

static
void
BmWimFree(
    void* Memory,
    void* Context)
{
    UNREFERENCED_PARAMETER(Context);
    if (Memory)
        FwFree(Memory);
}

BOOLEAN
BmLoadWindowsLogo(
    _In_ PVOID Device,
    _Out_ PUI_IMAGE Image)
{
    WimCoreAllocator Allocator = { BmWimAllocate, BmWimFree, NULL };
    WimCoreCtx Wim;
    const WimDentry* Root;
    PUCHAR Resources, Archive;
    uint8_t* Bitmap = NULL;
    size_t BitmapSize = 0;
    ULONG ResourcesSize, ArchiveSize, Name;
    size_t Child;
    BOOLEAN Loaded = FALSE;

    RtlZeroMemory(Image, sizeof(*Image));
    Resources = FwReadDeviceFile(Device, BM_LOGO_RESOURCES, &ResourcesSize);
    if (!Resources)
        return FALSE;

    Archive = BmFindRcData(Resources, ResourcesSize, BM_LOGO_RESOURCE, &ArchiveSize);
    wimcore_init(&Wim);
    if (Archive &&
        wimcore_open_memory(&Wim, &Allocator, Archive, ArchiveSize) == 0 &&
        wimcore_select_image(&Wim, 1) == 0 &&
        (Root = wimcore_get_root(&Wim, 1)) != NULL)
    {
        for (Name = 0; Name < RTL_NUMBER_OF(BmLogoNames) && !Loaded; Name++)
        {
            for (Child = 0; Child < Root->child_count; Child++)
            {
                if (!Root->children[Child].name_utf8 ||
                    _stricmp(Root->children[Child].name_utf8, BmLogoNames[Name]) != 0)
                {
                    continue;
                }

                if (wimcore_read_blob(&Wim, Root->children[Child].sha1, &Bitmap, &BitmapSize) == 0)
                {
                    Loaded = BmDecodeLogo(Bitmap, BitmapSize, Image);
                    BmWimFree(Bitmap, NULL);
                }
                break;
            }
        }
    }

    wimcore_close(&Wim);
    FwFreePages(Resources, ResourcesSize);
    return Loaded;
}
