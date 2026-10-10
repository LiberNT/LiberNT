/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Configuration Manager Library - Incremental hive log recovery
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cmlib.h"
#define NDEBUG
#include <debug.h>

static
BOOLEAN
HvpIsLogBaseBlockValid(
    _In_ PHBASE_BLOCK BaseBlock)
{
    return BaseBlock->Signature == HV_HBLOCK_SIGNATURE &&
           BaseBlock->Major == HSYS_MAJOR &&
           BaseBlock->Type == HV_LOG_INCREMENTAL_TYPE &&
           BaseBlock->Format == HBASE_FORMAT_MEMORY &&
           BaseBlock->Sequence1 == BaseBlock->Sequence2 &&
           BaseBlock->Length && !(BaseBlock->Length % HBLOCK_SIZE) &&
           HvpHiveHeaderChecksum(BaseBlock) == BaseBlock->CheckSum;
}

static
PHV_LOG_ENTRY
HvpGetLogEntry(
    _In_ PUCHAR Log,
    _In_ ULONG LogSize,
    _In_ ULONG Offset,
    _In_ ULONG Sequence)
{
    PHV_LOG_ENTRY Entry;
    PHV_LOG_DIRTY_PAGE Page;
    ULONG DataSize, i;

    if (Offset > LogSize || LogSize - Offset < sizeof(HV_LOG_ENTRY))
        return NULL;

    Entry = (PHV_LOG_ENTRY)(Log + Offset);
    if (Entry->Signature != HV_LOG_ENTRY_SIGNATURE ||
        Entry->Sequence != Sequence ||
        Entry->Size < sizeof(HV_LOG_ENTRY) ||
        Entry->Size % HSECTOR_SIZE ||
        Entry->Size > LogSize - Offset ||
        !Entry->Length ||
        Entry->Length % HBLOCK_SIZE ||
        Entry->Length >= HCELL_TYPE_MASK ||
        Entry->DirtyPageCount > (Entry->Size - sizeof(HV_LOG_ENTRY)) / sizeof(HV_LOG_DIRTY_PAGE) ||
        Entry->Hash2 != HvpComputeLogHash(Entry, FIELD_OFFSET(HV_LOG_ENTRY, Hash2)) ||
        Entry->Hash1 != HvpComputeLogHash(Entry + 1, Entry->Size - sizeof(HV_LOG_ENTRY)))
    {
        return NULL;
    }

    Page = (PHV_LOG_DIRTY_PAGE)(Entry + 1);
    DataSize = Entry->Size - sizeof(HV_LOG_ENTRY) - Entry->DirtyPageCount * sizeof(HV_LOG_DIRTY_PAGE);
    for (i = 0; i < Entry->DirtyPageCount; i++)
    {
        if (!Page[i].Size ||
            Page[i].Size > DataSize ||
            Page[i].Offset > Entry->Length ||
            Page[i].Size > Entry->Length - Page[i].Offset)
        {
            return NULL;
        }
        DataSize -= Page[i].Size;
    }

    return Entry;
}

BOOLEAN
CMAPI
HvpApplyIncrementalLog(
    _Inout_opt_ PHBASE_BLOCK BaseBlock,
    _In_ ULONG ImageLength,
    _In_ PVOID Log,
    _In_ ULONG LogSize,
    _In_ BOOLEAN Apply,
    _Inout_opt_ PRTL_BITMAP AppliedBlocks,
    _Out_ PULONG RequiredLength)
{
    PHBASE_BLOCK LogBaseBlock = Log;
    PHV_LOG_ENTRY Entry;
    PHV_LOG_DIRTY_PAGE Page;
    PUCHAR Data;
    BOOLEAN PrimaryValid;
    ULONG Offset, Sequence, Length, Applied, i;

    *RequiredLength = 0;
    if (LogSize < HSECTOR_SIZE + sizeof(HV_LOG_ENTRY) || !HvpIsLogBaseBlockValid(LogBaseBlock))
        return FALSE;

    ASSERT(BaseBlock || !Apply);
    PrimaryValid = BaseBlock &&
                   BaseBlock->Signature == HV_HBLOCK_SIGNATURE &&
                   HvpHiveHeaderChecksum(BaseBlock) == BaseBlock->CheckSum;
    if (PrimaryValid &&
        (BaseBlock->Sequence1 == BaseBlock->Sequence2 ||
         LogBaseBlock->Sequence1 < BaseBlock->Sequence2))
    {
        return FALSE;
    }

    Length = PrimaryValid ? BaseBlock->Length : LogBaseBlock->Length;
    if (Length >= HCELL_TYPE_MASK)
        return FALSE;

    Applied = 0;
    Sequence = LogBaseBlock->Sequence1;
    for (Offset = HSECTOR_SIZE;
         (Entry = HvpGetLogEntry(Log, LogSize, Offset, Sequence)) != NULL;
         Offset += Entry->Size, Sequence++)
    {
        if (Entry->Length > Length)
            Length = Entry->Length;
        if (Apply)
        {
            if (!Applied && !PrimaryValid)
                RtlCopyMemory(BaseBlock, LogBaseBlock, HSECTOR_SIZE);

            ASSERT(Entry->Length <= ImageLength);
            Page = (PHV_LOG_DIRTY_PAGE)(Entry + 1);
            Data = (PUCHAR)(Page + Entry->DirtyPageCount);
            for (i = 0; i < Entry->DirtyPageCount; i++)
            {
                RtlCopyMemory((PUCHAR)BaseBlock + HBLOCK_SIZE + Page[i].Offset, Data, Page[i].Size);
                if (AppliedBlocks)
                {
                    RtlSetBits(AppliedBlocks,
                               Page[i].Offset / HBLOCK_SIZE,
                               (Page[i].Offset % HBLOCK_SIZE + Page[i].Size + HBLOCK_SIZE - 1) / HBLOCK_SIZE);
                }
                Data += Page[i].Size;
            }

            BaseBlock->Length = Entry->Length;
            BaseBlock->Flags = (BaseBlock->Flags & ~HV_LOG_ENTRY_FLAGS) | (Entry->Flags & HV_LOG_ENTRY_FLAGS);
        }
        Applied++;
    }

    if (!Applied)
        return FALSE;

    if (Apply)
    {
        DPRINT1("Applied %lu hive log entries up to sequence 0x%lx\n", Applied, Sequence - 1);
        BaseBlock->Type = HFILE_TYPE_PRIMARY;
        BaseBlock->Sequence1 = Sequence - 1;
        BaseBlock->Sequence2 = Sequence - 1;
        BaseBlock->CheckSum = HvpHiveHeaderChecksum(BaseBlock);
    }

    *RequiredLength = Length;
    return TRUE;
}

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
ULONG
CMAPI
HvpQueryFileSize(
    _In_ PHHIVE Hive,
    _In_ ULONG FileType)
{
    HANDLE FileHandle = ((PCMHIVE)Hive)->FileHandles[FileType];
    FILE_STANDARD_INFORMATION FileStandard;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    if (!FileHandle)
        return 0;

    Status = ZwQueryInformationFile(FileHandle,
                                    &IoStatusBlock,
                                    &FileStandard,
                                    sizeof(FileStandard),
                                    FileStandardInformation);
    if (!NT_SUCCESS(Status) || FileStandard.EndOfFile.HighPart)
        return 0;

    return FileStandard.EndOfFile.LowPart;
}

NTSTATUS
CMAPI
HvpRecoverHiveFromLog(
    _In_ PHHIVE Hive,
    _In_opt_ PHBASE_BLOCK PrimaryBaseBlock,
    _Out_ PHBASE_BLOCK *HiveData,
    _Out_ PULONG HiveDataSize,
    _Out_ PRTL_BITMAP AppliedBlocks)
{
    PHBASE_BLOCK Image;
    PVOID Log;
    PULONG BitmapBuffer;
    ULONG LogSize, PrimarySize, Required, ImageSize, BitmapSize;
    ULONG FileOffset;

    *HiveData = NULL;
    *HiveDataSize = 0;

    LogSize = ROUND_DOWN(HvpQueryFileSize(Hive, HFILE_TYPE_LOG), HSECTOR_SIZE);
    if (LogSize < HSECTOR_SIZE + sizeof(HV_LOG_ENTRY))
        return STATUS_NOT_FOUND;

    Log = Hive->Allocate(LogSize, TRUE, TAG_CM);
    if (!Log)
        return STATUS_INSUFFICIENT_RESOURCES;

    FileOffset = 0;
    if (!Hive->FileRead(Hive, HFILE_TYPE_LOG, &FileOffset, Log, LogSize) ||
        !HvpApplyIncrementalLog(PrimaryBaseBlock, 0, Log, LogSize, FALSE, NULL, &Required))
    {
        Hive->Free(Log, LogSize);
        return STATUS_NOT_FOUND;
    }

    ImageSize = HBLOCK_SIZE + Required;
    BitmapSize = ROUND_UP(Required / HBLOCK_SIZE, sizeof(ULONG) * 8) / 8;
    Image = Hive->Allocate(ImageSize, TRUE, TAG_CM);
    BitmapBuffer = Hive->Allocate(BitmapSize, TRUE, TAG_CM);
    if (!Image || !BitmapBuffer)
    {
        if (Image)
            Hive->Free(Image, ImageSize);
        if (BitmapBuffer)
            Hive->Free(BitmapBuffer, BitmapSize);
        Hive->Free(Log, LogSize);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Image, ImageSize);
    RtlInitializeBitMap(AppliedBlocks, BitmapBuffer, BitmapSize * 8);
    RtlClearAllBits(AppliedBlocks);

    if (PrimaryBaseBlock)
        RtlCopyMemory(Image, PrimaryBaseBlock, HBLOCK_SIZE);

    PrimarySize = ROUND_DOWN(HvpQueryFileSize(Hive, HFILE_TYPE_PRIMARY), HSECTOR_SIZE);
    PrimarySize = (PrimarySize > HBLOCK_SIZE) ? min(PrimarySize - HBLOCK_SIZE, Required) : 0;
    FileOffset = HBLOCK_SIZE;
    if (PrimarySize &&
        !Hive->FileRead(Hive, HFILE_TYPE_PRIMARY, &FileOffset, (PUCHAR)Image + HBLOCK_SIZE, PrimarySize))
    {
        DPRINT1("Failed to read the primary hive data for log recovery\n");
        Hive->Free(BitmapBuffer, BitmapSize);
        Hive->Free(Image, ImageSize);
        Hive->Free(Log, LogSize);
        return STATUS_REGISTRY_IO_FAILED;
    }

    HvpApplyIncrementalLog(Image, Required, Log, LogSize, TRUE, AppliedBlocks, &Required);
    Hive->Free(Log, LogSize);

    *HiveData = Image;
    *HiveDataSize = ImageSize;
    return STATUS_SUCCESS;
}
#endif

/* EOF */
