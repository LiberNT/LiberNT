/*
 * PROJECT:     LiberNT WIM library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Files of a WIM image as WOF pointer files or extracted files: tree walk, pointer reparse data, overlay registration and extraction
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wimboot.h"
#include "sha1.h"

#include <string.h>

#define WIMBOOT_PATH_MAX            4096
#define WIMBOOT_HASH_CHUNK          4096
#define WIMBOOT_WOF_TAG             0x80000017
#define WIMBOOT_WOF_VERSION         1
#define WIMBOOT_WOF_PROVIDER_WIM    1
#define WIMBOOT_PROVIDER_VERSION    2
#define WIMBOOT_OVERLAY_MAGIC       0x66436F57
#define WIMBOOT_OVERLAY_VERSION     1
#define WIMBOOT_OVERLAY_HEADER      24
#define WIMBOOT_OVERLAY_ENTRY       40
#define WIMBOOT_OVERLAY_LOCATION    0x68

typedef struct _WIMBOOT_WALK
{
    WimCoreCtx* Wim;
    WIMBOOT_VISIT Visit;
    void* Context;
    char* Path;
} WIMBOOT_WALK;

typedef struct _WIMBOOT_EXTRACT
{
    WimCoreCtx* Wim;
    WIMBOOT_WRITE Write;
    void* Context;
    uint8_t* Buffer;
    uint64_t Capacity;
    int Result;
} WIMBOOT_EXTRACT;

static uint8_t* WimBootPut32(uint8_t* Out, uint32_t Value)
{
    Out[0] = (uint8_t)Value;
    Out[1] = (uint8_t)(Value >> 8);
    Out[2] = (uint8_t)(Value >> 16);
    Out[3] = (uint8_t)(Value >> 24);
    return Out + 4;
}

static uint8_t* WimBootPut64(uint8_t* Out, uint64_t Value)
{
    Out = WimBootPut32(Out, (uint32_t)Value);
    return WimBootPut32(Out, (uint32_t)(Value >> 32));
}

static const WimBlob* WimBootFindBlob(const WimCoreCtx* Wim, const uint8_t* Hash)
{
    size_t Index;

    for (Index = 0; Index < Wim->BlobCount; Index++)
    {
        if (memcmp(Wim->Blobs[Index].sha1.hash, Hash, 20) == 0)
            return &Wim->Blobs[Index];
    }
    return NULL;
}

int WimBootOpen(WimCoreCtx* Wim, WIMBOOT_IMAGE* Image)
{
    return WimBootOpenImage(Wim, 0, Image);
}

int WimBootOpenImage(WimCoreCtx* Wim, uint32_t Index, WIMBOOT_IMAGE* Image)
{
    const WimDentry* Root;
    uint8_t Chunk[WIMBOOT_HASH_CHUNK];
    uint64_t Offset, Size, Done;
    uint32_t Bytes;
    Sha1Ctx Hash;

    if (Index == 0)
        Index = Wim->Header.boot_index ? Wim->Header.boot_index : 1;
    if (Index > Wim->Header.image_count || wimcore_select_image(Wim, (int)Index) != 0)
        return WIMBOOT_BAD_WIM;

    Root = wimcore_get_root(Wim, (int)Index);
    Offset = Wim->Header.lookup_table.offset;
    Size = reshdr_get_size(&Wim->Header.lookup_table);
    if (!Root || !(Root->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY) ||
        Offset > Wim->ImageSize || Size > Wim->ImageSize - Offset)
    {
        return WIMBOOT_BAD_WIM;
    }

    sha1_init(&Hash);
    for (Done = 0; Done < Size; Done += Bytes)
    {
        Bytes = Size - Done < WIMBOOT_HASH_CHUNK ? (uint32_t)(Size - Done) : WIMBOOT_HASH_CHUNK;
        if (Wim->MemoryBase)
            memcpy(Chunk, Wim->MemoryBase + Offset + Done, Bytes);
        else if (!Wim->Read || Wim->Read(Wim->ReadContext, Offset + Done, Chunk, Bytes) != 0)
            return WIMBOOT_IO_ERROR;
        sha1_update(&Hash, Chunk, Bytes);
    }
    sha1_final(&Hash, Image->BlobTableHash);

    memcpy(Image->Guid, Wim->Header.guid, sizeof(Image->Guid));
    Image->ImageIndex = Index;
    return WIMBOOT_OK;
}

static int WimBootVisitChildren(WIMBOOT_WALK* Walk, const WimDentry* Directory, size_t PathLength)
{
    static const uint8_t NoData[20] = { 0 };
    const WimDentry* Child;
    const WimBlob* Blob;
    WIMBOOT_ENTRY Entry;
    size_t NameLength;
    size_t Length;
    size_t Index;
    int Result;

    for (Index = 0; Index < Directory->child_count; Index++)
    {
        Child = &Directory->children[Index];
        if (!Child->name_utf8 || !Child->name_utf8[0] ||
            strchr(Child->name_utf8, '/') || strchr(Child->name_utf8, '\\') ||
            strcmp(Child->name_utf8, ".") == 0 || strcmp(Child->name_utf8, "..") == 0)
        {
            return WIMBOOT_BAD_WIM;
        }

        NameLength = strlen(Child->name_utf8);
        Length = PathLength + (PathLength ? 1 : 0) + NameLength;
        if (Length >= WIMBOOT_PATH_MAX)
            return WIMBOOT_BAD_WIM;
        if (PathLength)
            Walk->Path[PathLength] = '/';
        memcpy(Walk->Path + PathLength + (PathLength ? 1 : 0), Child->name_utf8, NameLength + 1);

        memset(&Entry, 0, sizeof(Entry));
        Entry.Path = Walk->Path;
        Entry.Attributes = Child->attributes;
        Entry.IsDirectory = (Child->attributes & WIM_FILE_ATTRIBUTE_DIRECTORY) != 0;
        Entry.CreationTime = Child->creation_time;
        Entry.LastAccessTime = Child->last_access_time;
        Entry.LastWriteTime = Child->last_write_time;
        memcpy(Entry.Hash, Child->sha1, sizeof(Entry.Hash));
        if (!Entry.IsDirectory && memcmp(Child->sha1, NoData, sizeof(NoData)) != 0)
        {
            Blob = WimBootFindBlob(Walk->Wim, Child->sha1);
            if (!Blob || (Blob->flags & (WIM_RESHDR_FLAG_METADATA | WIM_RESHDR_FLAG_SPANNED)) ||
                Blob->original_size == 0 || Blob->compressed_size == 0 ||
                ((Blob->flags & WIM_RESHDR_FLAG_COMPRESSED)
                    ? Blob->compressed_size >= Blob->original_size
                    : Blob->compressed_size != Blob->original_size))
            {
                return WIMBOOT_BAD_WIM;
            }
            Entry.Size = Blob->original_size;
            Entry.StoredSize = Blob->compressed_size;
            Entry.Offset = Blob->offset;
        }

        if (Walk->Visit(Walk->Context, &Entry) != 0)
            return WIMBOOT_STOPPED;
        if (Entry.IsDirectory)
        {
            Result = WimBootVisitChildren(Walk, Child, Length);
            if (Result != WIMBOOT_OK)
                return Result;
        }
        Walk->Path[PathLength] = '\0';
    }
    return WIMBOOT_OK;
}

int WimBootWalk(WimCoreCtx* Wim, const WIMBOOT_IMAGE* Image, WIMBOOT_VISIT Visit, void* Context)
{
    const WimDentry* Root;
    WIMBOOT_WALK Walk;
    int Result;

    Root = wimcore_get_root(Wim, (int)Image->ImageIndex);
    if (!Root)
        return WIMBOOT_BAD_WIM;

    Walk.Wim = Wim;
    Walk.Visit = Visit;
    Walk.Context = Context;
    Walk.Path = (char*)Wim->Allocator.Alloc(WIMBOOT_PATH_MAX, Wim->Allocator.Context);
    if (!Walk.Path)
        return WIMBOOT_NO_MEMORY;
    Walk.Path[0] = '\0';

    Result = WimBootVisitChildren(&Walk, Root, 0);
    Wim->Allocator.Free(Walk.Path, Wim->Allocator.Context);
    return Result;
}

static int WimBootExtractEntry(void* Context, const WIMBOOT_ENTRY* Entry)
{
    WIMBOOT_EXTRACT* Extract = (WIMBOOT_EXTRACT*)Context;
    WimCoreAllocator* Allocator = &Extract->Wim->Allocator;
    uint8_t* Buffer;

    if (Entry->IsDirectory || Entry->Size == 0)
    {
        Extract->Result = Extract->Write(Extract->Context, Entry, NULL) == 0 ? WIMBOOT_OK : WIMBOOT_STOPPED;
        return Extract->Result != WIMBOOT_OK;
    }

    if (Entry->Size > SIZE_MAX)
    {
        Extract->Result = WIMBOOT_NO_MEMORY;
        return 1;
    }
    if (Entry->Size > Extract->Capacity)
    {
        Buffer = (uint8_t*)Allocator->Alloc((size_t)Entry->Size, Allocator->Context);
        if (!Buffer)
        {
            Extract->Result = WIMBOOT_NO_MEMORY;
            return 1;
        }
        if (Extract->Buffer)
            Allocator->Free(Extract->Buffer, Allocator->Context);
        Extract->Buffer = Buffer;
        Extract->Capacity = Entry->Size;
    }

    if (wimcore_read_blob_into(Extract->Wim, Entry->Hash, Extract->Buffer, (size_t)Entry->Size, NULL, NULL, Entry->Path) != 0)
    {
        Extract->Result = WIMBOOT_IO_ERROR;
        return 1;
    }
    Extract->Result = Extract->Write(Extract->Context, Entry, Extract->Buffer) == 0 ? WIMBOOT_OK : WIMBOOT_STOPPED;
    return Extract->Result != WIMBOOT_OK;
}

int WimBootExtract(WimCoreCtx* Wim, const WIMBOOT_IMAGE* Image, WIMBOOT_WRITE Write, void* Context)
{
    WIMBOOT_EXTRACT Extract;
    int Result;

    Extract.Wim = Wim;
    Extract.Write = Write;
    Extract.Context = Context;
    Extract.Buffer = NULL;
    Extract.Capacity = 0;
    Extract.Result = WIMBOOT_OK;

    Result = WimBootWalk(Wim, Image, WimBootExtractEntry, &Extract);
    if (Extract.Buffer)
        Wim->Allocator.Free(Extract.Buffer, Wim->Allocator.Context);
    return Result == WIMBOOT_STOPPED ? Extract.Result : Result;
}

void WimBootPointer(uint8_t Buffer[WIMBOOT_POINTER_SIZE], const WIMBOOT_IMAGE* Image, const WIMBOOT_ENTRY* Entry)
{
    uint8_t* Out = Buffer;

    Out = WimBootPut32(Out, WIMBOOT_WOF_TAG);
    Out = WimBootPut32(Out, WIMBOOT_POINTER_SIZE - 8);
    Out = WimBootPut32(Out, WIMBOOT_WOF_VERSION);
    Out = WimBootPut32(Out, WIMBOOT_WOF_PROVIDER_WIM);
    Out = WimBootPut32(Out, WIMBOOT_PROVIDER_VERSION);
    Out = WimBootPut32(Out, 0);
    Out = WimBootPut64(Out, 0);
    memcpy(Out, Entry->Hash, 20);
    Out += 20;
    memcpy(Out, Image->BlobTableHash, 20);
    Out += 20;
    Out = WimBootPut64(Out, Entry->Size);
    Out = WimBootPut64(Out, Entry->StoredSize);
    Out = WimBootPut64(Out, Entry->Offset);
}

/* The values of the fields Microsoft does not document are the ones wimlib's wimboot.c writes */
uint32_t WimBootOverlay(uint8_t* Buffer,
                        uint32_t Capacity,
                        const WIMBOOT_IMAGE* Image,
                        const uint16_t* Path,
                        uint64_t PartitionOffset,
                        uint32_t DiskSignature)
{
    uint8_t* Out = Buffer;
    uint32_t Characters = 0;
    uint32_t Location;
    uint32_t Total;
    uint32_t Index;

    while (Path[Characters] != 0)
        Characters++;
    if (Characters == 0 || Path[0] != '\\')
        return 0;
    Location = WIMBOOT_OVERLAY_LOCATION + (Characters + 1) * 2;
    Total = WIMBOOT_OVERLAY_HEADER + WIMBOOT_OVERLAY_ENTRY + Location;
    if (Capacity < Total)
        return 0;

    Out = WimBootPut32(Out, WIMBOOT_OVERLAY_MAGIC);
    Out = WimBootPut32(Out, WIMBOOT_OVERLAY_VERSION);
    Out = WimBootPut32(Out, 0x28);
    Out = WimBootPut32(Out, 1);
    Out = WimBootPut64(Out, 1);

    Out = WimBootPut64(Out, 0);
    Out = WimBootPut32(Out, WIMBOOT_OVERLAY_HEADER + WIMBOOT_OVERLAY_ENTRY);
    Out = WimBootPut32(Out, Location);
    Out = WimBootPut32(Out, 0);
    Out = WimBootPut32(Out, Image->ImageIndex);
    memcpy(Out, Image->Guid, 16);
    Out += 16;

    Out = WimBootPut32(Out, 0);
    Out = WimBootPut32(Out, 0);
    Out = WimBootPut32(Out, Location);
    Out = WimBootPut32(Out, 0);
    Out = WimBootPut32(Out, 5);
    Out = WimBootPut32(Out, 1);
    Out = WimBootPut32(Out, Location - 0x14);
    Out = WimBootPut32(Out, 5);
    Out = WimBootPut32(Out, 6);
    Out = WimBootPut32(Out, 0);
    Out = WimBootPut32(Out, 0x48);
    Out = WimBootPut32(Out, 0);
    Out = WimBootPut64(Out, PartitionOffset);
    Out = WimBootPut64(Out, 0);
    Out = WimBootPut32(Out, 0);
    Out = WimBootPut32(Out, 1);
    Out = WimBootPut32(Out, DiskSignature);
    Out = WimBootPut32(Out, 0);
    Out = WimBootPut64(Out, 0);
    Out = WimBootPut64(Out, 0);
    Out = WimBootPut64(Out, 0);

    for (Index = 0; Index <= Characters; Index++)
    {
        Out[0] = (uint8_t)Path[Index];
        Out[1] = (uint8_t)(Path[Index] >> 8);
        Out += 2;
    }
    return Total;
}
