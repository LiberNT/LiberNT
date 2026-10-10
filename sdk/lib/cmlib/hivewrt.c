/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Configuration Manager Library - Registry Syncing & Hive/Log/Alternate Writing
 * COPYRIGHT:   Copyright 2001 - 2005 Eric Kohl
 *              Copyright 2005 Filip Navara <navaraf@reactos.org>
 *              Copyright 2021 Max Korostil
 *              Copyright 2022 George Bișoc <george.bisoc@reactos.org>
 */

#include "cmlib.h"
#define NDEBUG
#include <debug.h>

/* DECLARATIONS *************************************************************/

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
BOOLEAN
NTAPI
IoSetThreadHardErrorMode(
    _In_ BOOLEAN HardErrorEnabled);
#endif

/* GLOBALS ******************************************************************/

/* PRIVATE FUNCTIONS ********************************************************/

/**
 * @brief
 * Validates the base block header of a primary
 * hive for consistency.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor to look
 * for the header block.
 */
static
VOID
HvpValidateBaseHeader(
    _In_ PHHIVE RegistryHive)
{
    PHBASE_BLOCK BaseBlock;

    /*
     * Cache the base block and validate it.
     * Especially...
     *
     * 1. It must must have a valid signature.
     * 2. It must have a valid format.
     * 3. It must be of an adequate major version,
     *    not anything else.
     */
    BaseBlock = RegistryHive->BaseBlock;
    ASSERT(BaseBlock->Signature == HV_HBLOCK_SIGNATURE);
    ASSERT(BaseBlock->Format == HBASE_FORMAT_MEMORY);
    ASSERT(BaseBlock->Major == HSYS_MAJOR);
}

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
#define HV_LOG_FILE_SIZE_CAP (4 * 1024 * 1024)
#define HV_LOG_FILE_GROWTH (1024 * 1024)

static
BOOLEAN
HvpIsLogOnlyHive(
    _In_ PHHIVE RegistryHive)
{
    return ((PCMHIVE)RegistryHive)->FileHandles[HFILE_TYPE_LOG] != NULL;
}

static
BOOLEAN
HvpReserveLog(
    _In_ PHHIVE RegistryHive,
    _In_ ULONG End)
{
    PCMHIVE CmHive = (PCMHIVE)RegistryHive;
    ULONG Size = CmHive->LogFileSizes[0].LowPart;

    if (End <= Size)
        return TRUE;
    if (!Size)
    {
        Size = HvpQueryFileSize(RegistryHive, HFILE_TYPE_LOG);
        CmHive->LogFileSizes[0].QuadPart = Size;
        if (End <= Size)
            return TRUE;
    }

    End = ROUND_UP(End, HV_LOG_FILE_GROWTH);
    if (!CmpFileSetSize(RegistryHive, HFILE_TYPE_LOG, End, Size))
        return FALSE;
    CmHive->LogFileSizes[0].QuadPart = End;
    return TRUE;
}

static
BOOLEAN
HvpPrepareUnreconciledVector(
    _In_ PHHIVE RegistryHive)
{
    ULONG Bits = RegistryHive->DirtyVector.SizeOfBitMap;
    PULONG Buffer;

    if (RegistryHive->UnreconciledVector.Buffer &&
        RegistryHive->UnreconciledVector.SizeOfBitMap >= Bits)
    {
        return TRUE;
    }

    Buffer = RegistryHive->Allocate(Bits / 8, TRUE, TAG_CM);
    if (!Buffer)
        return FALSE;
    RtlZeroMemory(Buffer, Bits / 8);
    if (RegistryHive->UnreconciledVector.Buffer)
    {
        RtlCopyMemory(Buffer, RegistryHive->UnreconciledVector.Buffer,
                      RegistryHive->UnreconciledVector.SizeOfBitMap / 8);
        RegistryHive->Free(RegistryHive->UnreconciledVector.Buffer, 0);
    }
    RtlInitializeBitMap(&RegistryHive->UnreconciledVector, Buffer, Bits);
    return TRUE;
}

static
VOID
HvpMergeUnreconciledVector(
    _In_ PHHIVE RegistryHive)
{
    ULONG Index;

    for (Index = 0; Index < RegistryHive->DirtyVector.SizeOfBitMap / 32; Index++)
        RegistryHive->UnreconciledVector.Buffer[Index] |= RegistryHive->DirtyVector.Buffer[Index];
    RegistryHive->UnreconciledCount = RtlNumberOfSetBits(&RegistryHive->UnreconciledVector);
    RtlClearAllBits(&RegistryHive->DirtyVector);
    RegistryHive->DirtyCount = 0;
}
#endif

static
BOOLEAN
CMAPI
HvpWriteLog(
    _In_ PHHIVE RegistryHive,
    _In_ BOOLEAN Append)
{
    PHBASE_BLOCK BaseBlock = RegistryHive->BaseBlock;
    ULONG BlockCount = RegistryHive->Storage[Stable].Length / HBLOCK_SIZE;
    ULONG BlockIndex, RunEnd, RunCount, DirtyBlocks, Sequence;
    ULONG HeaderSize, EntrySize, BufferSize, FileOffset, LogOffset;
    PUCHAR Buffer, Data;
    PHBASE_BLOCK LogBaseBlock;
    PHV_LOG_ENTRY Entry;
    PHV_LOG_DIRTY_PAGE Page;
    BOOLEAN NewCycle, Success;

    if (BaseBlock->Sequence1 != BaseBlock->Sequence2)
        return FALSE;

    RunCount = 0;
    DirtyBlocks = 0;
    for (BlockIndex = 0; BlockIndex < BlockCount; BlockIndex = RunEnd + 1)
    {
        RunEnd = BlockIndex;
        if (!RtlCheckBit(&RegistryHive->DirtyVector, BlockIndex))
            continue;
        while (RunEnd + 1 < BlockCount && RtlCheckBit(&RegistryHive->DirtyVector, RunEnd + 1))
            RunEnd++;
        RunCount++;
        DirtyBlocks += RunEnd - BlockIndex + 1;
    }
    if (!RunCount)
        return TRUE;

    NewCycle = !Append || !RegistryHive->CurrentLogOffset;
    HeaderSize = NewCycle ? HSECTOR_SIZE : 0;
    FileOffset = NewCycle ? 0 : RegistryHive->CurrentLogOffset;
    LogOffset = FileOffset;
    Sequence = Append ? BaseBlock->Sequence1 + 1 : BaseBlock->Sequence1;
    BufferSize = ROUND_UP(HeaderSize + sizeof(HV_LOG_ENTRY) +
                          RunCount * sizeof(HV_LOG_DIRTY_PAGE) +
                          DirtyBlocks * HBLOCK_SIZE, HBLOCK_SIZE);
    EntrySize = BufferSize - HeaderSize;
    Buffer = RegistryHive->Allocate(BufferSize, TRUE, TAG_CM);
    if (!Buffer)
    {
        DPRINT1("Failed to allocate 0x%lx bytes for the hive log entry\n", BufferSize);
        return FALSE;
    }
    RtlZeroMemory(Buffer, BufferSize);

    if (HeaderSize)
    {
        LogBaseBlock = (PHBASE_BLOCK)Buffer;
        RtlCopyMemory(LogBaseBlock, BaseBlock, HSECTOR_SIZE);
        LogBaseBlock->Type = HV_LOG_INCREMENTAL_TYPE;
        LogBaseBlock->Sequence1 = Sequence;
        LogBaseBlock->Sequence2 = Sequence;
        LogBaseBlock->CheckSum = HvpHiveHeaderChecksum(LogBaseBlock);
    }

    Entry = (PHV_LOG_ENTRY)(Buffer + HeaderSize);
    Entry->Signature = HV_LOG_ENTRY_SIGNATURE;
    Entry->Size = EntrySize;
    Entry->Flags = BaseBlock->Flags & HV_LOG_ENTRY_FLAGS;
    Entry->Sequence = Sequence;
    Entry->Length = BaseBlock->Length;
    Entry->DirtyPageCount = RunCount;

    Page = (PHV_LOG_DIRTY_PAGE)(Entry + 1);
    Data = (PUCHAR)(Page + RunCount);
    for (BlockIndex = 0; BlockIndex < BlockCount; BlockIndex++)
    {
        if (!RtlCheckBit(&RegistryHive->DirtyVector, BlockIndex))
            continue;
        if (!BlockIndex || !RtlCheckBit(&RegistryHive->DirtyVector, BlockIndex - 1))
        {
            Page->Offset = BlockIndex * HBLOCK_SIZE;
            Page->Size = 0;
            Page++;
        }
        Page[-1].Size += HBLOCK_SIZE;
        RtlCopyMemory(Data, HvpLookupBlock(RegistryHive, Stable, BlockIndex), HBLOCK_SIZE);
        Data += HBLOCK_SIZE;
    }

    Entry->Hash1 = HvpComputeLogHash(Entry + 1, EntrySize - sizeof(HV_LOG_ENTRY));
    Entry->Hash2 = HvpComputeLogHash(Entry, FIELD_OFFSET(HV_LOG_ENTRY, Hash2));

    Success = TRUE;
#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    if (Append)
        Success = HvpReserveLog(RegistryHive, FileOffset + BufferSize);
#endif
    if (Success)
    {
        Success = RegistryHive->FileWrite(RegistryHive, HFILE_TYPE_LOG,
                                          &FileOffset, Buffer, BufferSize);
    }
    RegistryHive->Free(Buffer, BufferSize);
    if (!Success)
    {
        DPRINT1("Failed to write the hive log entry (sequence 0x%x)\n", Sequence);
        return FALSE;
    }

    if (!CmpFileFlush(RegistryHive, HFILE_TYPE_LOG, NULL, 0))
    {
        DPRINT1("Failed to flush the hive log\n");
        return FALSE;
    }

    if (!Append)
        return TRUE;

    if (NewCycle)
    {
        BaseBlock->Type = HFILE_TYPE_PRIMARY;
        BaseBlock->Sequence1 = Sequence;
        BaseBlock->Sequence2 = Sequence - 1;
        BaseBlock->CheckSum = HvpHiveHeaderChecksum(BaseBlock);
        FileOffset = 0;
        Success = RegistryHive->FileWrite(RegistryHive, HFILE_TYPE_PRIMARY,
                                          &FileOffset, BaseBlock, sizeof(HBASE_BLOCK)) &&
                  CmpFileFlush(RegistryHive, HFILE_TYPE_PRIMARY, NULL, 0);
        if (!Success)
        {
            DPRINT1("Failed to mark the primary hive as logged\n");
            BaseBlock->Sequence1 = Sequence - 1;
            BaseBlock->CheckSum = HvpHiveHeaderChecksum(BaseBlock);
            return FALSE;
        }
    }

    BaseBlock->Sequence1 = Sequence;
    BaseBlock->Sequence2 = Sequence;
    BaseBlock->CheckSum = HvpHiveHeaderChecksum(BaseBlock);
    RegistryHive->CurrentLogOffset = LogOffset + BufferSize;
    RegistryHive->CurrentLogSequence = Sequence;
    return TRUE;
}

/**
 * @brief
 * Writes data (dirty or non) to a primary hive during
 * syncing operation. Hive writing is also performed
 * during a flush occurrence on request by the system.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor where the data is
 * to be written to that hive.
 *
 * @param[in] OnlyDirty
 * If set to TRUE, the function only looks for dirty
 * data to be written to the primary hive, otherwise if
 * it's set to FALSE then the function writes all the data.
 *
 * @param[in] FileType
 * The file type of a registry hive. This can be HFILE_TYPE_PRIMARY
 * or HFILE_TYPE_ALTERNATE.
 *
 * @return
 * Returns TRUE if writing to hive has succeeded,
 * FALSE otherwise.
 *
 * @remarks
 * The on-disk header metadata of a hive is already written with type
 * of HFILE_TYPE_PRIMARY, regardless of what file type the caller submits,
 * as an alternate hive is basically a mirror of the primary hive.
 */
static
BOOLEAN
CMAPI
HvpWriteHive(
    _In_ PHHIVE RegistryHive,
    _In_ BOOLEAN OnlyDirty,
    _In_ ULONG FileType,
    _In_ BOOLEAN PrimaryDirty,
    _In_ PRTL_BITMAP BlockVector)
{
    BOOLEAN Success;
    ULONG FileOffset;
    ULONG BlockIndex;
    ULONG LastIndex;
    PVOID Block;

    ASSERT(!RegistryHive->ReadOnly);
    ASSERT(RegistryHive->BaseBlock->Length ==
           RegistryHive->Storage[Stable].Length);
    ASSERT(RegistryHive->BaseBlock->RootCell != HCELL_NIL);

    /* Validate the base header before we go further */
    HvpValidateBaseHeader(RegistryHive);

    /*
     * The sequences can diverge during a forced system shutdown
     * occurrence, such as during a power failure, a hardware
     * failure or during a system crash, and when one of the
     * sequences have been modified during writing into the log
     * or hive. In such cases the hive needs a repair.
     */
    if (RegistryHive->BaseBlock->Sequence1 !=
        RegistryHive->BaseBlock->Sequence2)
    {
        DPRINT1("The sequences DO NOT MATCH (Sequence1 == 0x%x, Sequence2 == 0x%x)\n",
                RegistryHive->BaseBlock->Sequence1, RegistryHive->BaseBlock->Sequence2);
        return FALSE;
    }

    /*
     * Update the primary sequence number and write
     * the base block to hive.
     */
    RegistryHive->BaseBlock->Type = HFILE_TYPE_PRIMARY;
    RegistryHive->BaseBlock->Sequence1++;
    RegistryHive->BaseBlock->CheckSum = HvpHiveHeaderChecksum(RegistryHive->BaseBlock);

    if (!PrimaryDirty)
    {
        /* Write hive block */
        FileOffset = 0;
        Success = RegistryHive->FileWrite(RegistryHive, FileType,
                                          &FileOffset, RegistryHive->BaseBlock,
                                          sizeof(HBASE_BLOCK));
        if (!Success)
        {
            DPRINT1("Failed to write the base block header to primary hive (primary sequence)\n");
            return FALSE;
        }

        if (!CmpFileFlush(RegistryHive, FileType, NULL, 0))
        {
            DPRINT1("Failed to flush the primary hive base block\n");
            return FALSE;
        }
    }

    /* Write the whole primary hive, block by block */
    BlockIndex = 0;
    while (BlockIndex < RegistryHive->Storage[Stable].Length / HBLOCK_SIZE)
    {
        /*
         * If we have to synchronize the registry hive we
         * want to look for dirty blocks to reflect the new
         * updates done to the hive. Otherwise just write
         * all the blocks as if we were doing a regular
         * hive write.
         */
        if (OnlyDirty)
        {
            /* Check if the block is clean or we're past the last block */
            LastIndex = BlockIndex;
            BlockIndex = RtlFindSetBits(BlockVector, 1, BlockIndex);
            if (BlockIndex == ~HV_CLEAN_BLOCK || BlockIndex < LastIndex ||
                BlockIndex >= RegistryHive->Storage[Stable].Length / HBLOCK_SIZE)
            {
                break;
            }
        }

        /* Get the block and offset position */
        Block = HvpLookupBlock(RegistryHive, Stable, BlockIndex);
        FileOffset = (BlockIndex + 1) * HBLOCK_SIZE;

        /* Now write this block to primary hive file */
        Success = RegistryHive->FileWrite(RegistryHive, FileType,
                                          &FileOffset, Block, HBLOCK_SIZE);
        if (!Success)
        {
            DPRINT1("Failed to write hive block to primary hive file (block 0x%p, block index 0x%x)\n",
                    Block, BlockIndex);
            return FALSE;
        }

        /* Go to the next block */
        BlockIndex++;
    }

    /*
     * We wrote all the hive contents to the file, we
     * must flush the changes to disk now.
     */
    Success = CmpFileFlush(RegistryHive, FileType, NULL, 0);
    if (!Success)
    {
        DPRINT1("Failed to flush the primary hive\n");
        return FALSE;
    }

    /*
     * Increment the secondary sequence number and
     * update the checksum. A successful hive write
     * transaction is when both of sequences are the
     * same, indicating the write operation didn't
     * fail.
     */
    RegistryHive->BaseBlock->Sequence2++;
    RegistryHive->BaseBlock->CheckSum = HvpHiveHeaderChecksum(RegistryHive->BaseBlock);

    /* Write hive block */
    FileOffset = 0;
    Success = RegistryHive->FileWrite(RegistryHive, FileType,
                                      &FileOffset, RegistryHive->BaseBlock,
                                      sizeof(HBASE_BLOCK));
    if (!Success)
    {
        DPRINT1("Failed to write the base block header to primary hive (secondary sequence)\n");
        return FALSE;
    }

    return TRUE;
}

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
static
BOOLEAN
CMAPI
HvpReconcileHive(
    _In_ PHHIVE RegistryHive)
{
    if (!RegistryHive->CurrentLogOffset)
        return TRUE;

    if (!HvpWriteHive(RegistryHive, TRUE, HFILE_TYPE_PRIMARY, TRUE, &RegistryHive->UnreconciledVector) ||
        !CmpFileFlush(RegistryHive, HFILE_TYPE_PRIMARY, NULL, 0))
    {
        return FALSE;
    }

    RtlClearAllBits(&RegistryHive->UnreconciledVector);
    RegistryHive->UnreconciledCount = 0;
    RegistryHive->CurrentLogOffset = 0;
    return TRUE;
}
#endif

/* PUBLIC FUNCTIONS ***********************************************************/

/**
 * @brief
 * Synchronizes a registry hive with latest updates
 * from dirty data present in volatile memory, aka RAM.
 * It writes both to hive log and corresponding primary
 * hive. Syncing is done on request by the system during
 * a flush occurrence.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor where syncing is
 * to be performed.
 *
 * @return
 * Returns TRUE if syncing has succeeded, FALSE otherwise.
 */
BOOLEAN
CMAPI
HvSyncHive(
    _In_ PHHIVE RegistryHive)
{
#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    BOOLEAN HardErrors;
#endif
    BOOLEAN LogOnly;

    ASSERT(!RegistryHive->ReadOnly);
    ASSERT(RegistryHive->Signature == HV_HHIVE_SIGNATURE);

    /* Avoid any write operations on volatile hives */
    if (RegistryHive->HiveFlags & HIVE_VOLATILE)
    {
        DPRINT("Hive 0x%p is volatile\n", RegistryHive);
        return TRUE;
    }

    /*
     * Check if there's any dirty data in the vector.
     * A space with clean blocks would be pointless for
     * a log because we want to write dirty data in and
     * sync up, not clean data. So just consider our
     * job as done as there's literally nothing to do.
     */
    if (RtlFindSetBits(&RegistryHive->DirtyVector, 1, 0) == ~HV_CLEAN_BLOCK)
    {
        DPRINT("The dirty vector has clean data, nothing to do\n");
        return TRUE;
    }

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    /* Disable hard errors before syncing the hive */
    HardErrors = IoSetThreadHardErrorMode(FALSE);
#endif

#if !defined(_BLDR_)
    /* Update hive header modification time */
    KeQuerySystemTime(&RegistryHive->BaseBlock->TimeStamp);
#endif


#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    LogOnly = HvpIsLogOnlyHive(RegistryHive);
    if (LogOnly && !HvpPrepareUnreconciledVector(RegistryHive))
    {
        if (RegistryHive->CurrentLogOffset && !HvpReconcileHive(RegistryHive))
        {
            IoSetThreadHardErrorMode(HardErrors);
            return FALSE;
        }
        LogOnly = FALSE;
    }
#else
    LogOnly = FALSE;
#endif
    if (!HvpWriteLog(RegistryHive, LogOnly))
    {
        DPRINT1("Failed to write the hive log\n");
#if !defined(CMLIB_HOST) && !defined(_BLDR_)
        IoSetThreadHardErrorMode(HardErrors);
#endif
        return FALSE;
    }

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    if (LogOnly)
    {
        HvpMergeUnreconciledVector(RegistryHive);
        if (RegistryHive->CurrentLogOffset >= HV_LOG_FILE_SIZE_CAP)
        {
            if (!HvpReconcileHive(RegistryHive))
            {
                DPRINT1("Failed to reconcile the primary hive\n");
                IoSetThreadHardErrorMode(HardErrors);
                return FALSE;
            }
        }
    }
    else
#endif
    {
        /* Update the primary hive file */
        if (!HvpWriteHive(RegistryHive, TRUE, HFILE_TYPE_PRIMARY, FALSE, &RegistryHive->DirtyVector))
        {
            DPRINT1("Failed to write the primary hive\n");
#if !defined(CMLIB_HOST) && !defined(_BLDR_)
            IoSetThreadHardErrorMode(HardErrors);
#endif
            return FALSE;
        }

        /* Clear dirty bitmap. */
        RtlClearAllBits(&RegistryHive->DirtyVector);
        RegistryHive->DirtyCount = 0;
    }

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    IoSetThreadHardErrorMode(HardErrors);
#endif
    return TRUE;
}

/**
 * @unimplemented
 * @brief
 * Determines whether a registry hive needs
 * to be shrinked or not based on its overall
 * size of the hive space to avoid unnecessary
 * bloat.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor where hive
 * shrinking is to be determined.
 *
 * @return
 * Returns TRUE if hive shrinking needs to be
 * done, FALSE otherwise.
 */
BOOLEAN
CMAPI
HvHiveWillShrink(
    _In_ PHHIVE RegistryHive)
{
    /* No shrinking yet */
    UNIMPLEMENTED_ONCE;
    return FALSE;
}

/**
 * @brief
 * Writes data to a registry hive. Unlike
 * HvSyncHive, this function just writes
 * the wholy registry data to a primary hive,
 * ignoring if a certain data block is dirty
 * or not.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor where data
 * is be written into.
 *
 * @return
 * Returns TRUE if hive writing has succeeded,
 * FALSE otherwise.
 */
BOOLEAN
CMAPI
HvWriteHive(
    _In_ PHHIVE RegistryHive)
{
    ASSERT(!RegistryHive->ReadOnly);
    ASSERT(RegistryHive->Signature == HV_HHIVE_SIGNATURE);

#if !defined(_BLDR_)
    /* Update hive header modification time */
    KeQuerySystemTime(&RegistryHive->BaseBlock->TimeStamp);
#endif

    /* Update hive file */
    if (!HvpWriteHive(RegistryHive, FALSE, HFILE_TYPE_PRIMARY, FALSE, &RegistryHive->DirtyVector) ||
        !CmpFileFlush(RegistryHive, HFILE_TYPE_PRIMARY, NULL, 0))
    {
        DPRINT1("Failed to write the hive\n");
        return FALSE;
    }

    if (RegistryHive->UnreconciledVector.Buffer)
        RtlClearAllBits(&RegistryHive->UnreconciledVector);
    RegistryHive->UnreconciledCount = 0;
    RegistryHive->CurrentLogOffset = 0;
    return TRUE;
}

BOOLEAN
CMAPI
HvReconcileHive(
    _In_ PHHIVE RegistryHive)
{
#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    BOOLEAN HardErrors;
    BOOLEAN Success;
#endif

    if (!HvSyncHive(RegistryHive))
        return FALSE;

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    if (RegistryHive->HiveFlags & HIVE_VOLATILE)
        return TRUE;

    HardErrors = IoSetThreadHardErrorMode(FALSE);
    Success = HvpReconcileHive(RegistryHive);
    IoSetThreadHardErrorMode(HardErrors);
    return Success;
#else
    return TRUE;
#endif
}

/**
 * @brief
 * Synchronizes a hive with recovered
 * data during a healing/resuscitation
 * operation of the registry.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor where data
 * syncing is to be done.
 *
 * @return
 * Returns TRUE if hive syncing during recovery
 * succeeded, FALSE otherwise.
 */
BOOLEAN
CMAPI
HvSyncHiveFromRecover(
    _In_ PHHIVE RegistryHive)
{
    ASSERT(!RegistryHive->ReadOnly);
    ASSERT(RegistryHive->Signature == HV_HHIVE_SIGNATURE);

    /* Call the private API call to do the deed for us */
    return HvpWriteHive(RegistryHive, TRUE, HFILE_TYPE_PRIMARY, TRUE, &RegistryHive->DirtyVector) &&
           CmpFileFlush(RegistryHive, HFILE_TYPE_PRIMARY, NULL, 0);
}

/* EOF */
