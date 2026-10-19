/*
 *  FreeLoader NTFS support
 *  Copyright (C) 2004  Filip Navara  <xnavara@volny.cz>
 *  Copyright (C) 2009-2010  Hervé Poussineau
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * Limitations:
 * - No support for compressed files.
 * - May crash on corrupted filesystem.
 */

#include <bl.h>
#include "ntfs.h"


#define NTFS_MAX_ATTRIBUTE_LIST_RECURSION 8

typedef struct _NTFS_VOLUME_INFO
{
    NTFS_BOOTSECTOR BootSector;
    ULONG ClusterSize;
    ULONG MftRecordSize;
    ULONG IndexRecordSize;
    PNTFS_MFT_RECORD MasterFileTable;
    /* FIXME: MFTContext is never freed. */
    PNTFS_ATTR_CONTEXT MFTContext;
    DK_DEVICE Device;
} NTFS_VOLUME_INFO;

static ULONGLONG NtfsGetAttributeSize(PNTFS_ATTR_RECORD AttrRecord)
{
    if (AttrRecord->IsNonResident)
        return AttrRecord->NonResident.DataSize;
    else
        return AttrRecord->Resident.ValueLength;
}

static PUCHAR NtfsDecodeRun(PUCHAR DataRun, LONGLONG *DataRunOffset, ULONGLONG *DataRunLength)
{
    UCHAR DataRunOffsetSize;
    UCHAR DataRunLengthSize;
    CHAR i;

    DataRunOffsetSize = (*DataRun >> 4) & 0xF;
    DataRunLengthSize = *DataRun & 0xF;
    *DataRunOffset = 0;
    *DataRunLength = 0;
    DataRun++;
    for (i = 0; i < DataRunLengthSize; i++)
    {
        *DataRunLength += ((ULONG64)*DataRun) << (i * 8);
        DataRun++;
    }

    /* NTFS 3+ sparse files */
    if (DataRunOffsetSize == 0)
    {
        *DataRunOffset = -1;
    }
    else
    {
        for (i = 0; i < DataRunOffsetSize - 1; i++)
        {
            *DataRunOffset += ((ULONG64)*DataRun) << (i * 8);
            DataRun++;
        }
        /* The last byte contains sign so we must process it different way. */
        *DataRunOffset = ((LONG64)(CHAR)(*(DataRun++)) << (i * 8)) + *DataRunOffset;
    }

    return DataRun;
}

static PNTFS_ATTR_CONTEXT NtfsPrepareAttributeContext(PNTFS_ATTR_RECORD AttrRecord)
{
    PNTFS_ATTR_CONTEXT Context;

    Context = FwAllocate(FIELD_OFFSET(NTFS_ATTR_CONTEXT, Record) + AttrRecord->Length);
    RtlCopyMemory(&Context->Record, AttrRecord, AttrRecord->Length);
    if (AttrRecord->IsNonResident)
    {
        LONGLONG DataRunOffset;
        ULONGLONG DataRunLength;

        Context->CacheRun = (PUCHAR)&Context->Record + Context->Record.NonResident.MappingPairsOffset;
        Context->CacheRunOffset = 0;
        Context->CacheRun = NtfsDecodeRun(Context->CacheRun, &DataRunOffset, &DataRunLength);
        Context->CacheRunLength = DataRunLength;
        if (DataRunOffset != -1)
        {
            /* Normal run. */
            Context->CacheRunStartLCN =
            Context->CacheRunLastLCN = DataRunOffset;
        }
        else
        {
            /* Sparse run. */
            Context->CacheRunStartLCN = -1;
            Context->CacheRunLastLCN = 0;
        }
        Context->CacheRunCurrentOffset = 0;
    }

    return Context;
}

static VOID NtfsReleaseAttributeContext(PNTFS_ATTR_CONTEXT Context)
{
    FwFree(Context);
}

static BOOLEAN NtfsDiskRead(PNTFS_VOLUME_INFO Volume, ULONGLONG Offset, ULONGLONG Length, PCHAR Buffer)
{
    if (Length > MAXULONG)
        return FALSE;

    return DkRead(&Volume->Device, Offset, (ULONG)Length, Buffer);
}

static ULONG NtfsReadAttribute(PNTFS_VOLUME_INFO Volume, PNTFS_ATTR_CONTEXT Context, ULONGLONG Offset, PCHAR Buffer, ULONG Length)
{
    ULONGLONG LastLCN;
    PUCHAR DataRun;
    LONGLONG DataRunOffset;
    ULONGLONG DataRunLength;
    LONGLONG DataRunStartLCN;
    ULONGLONG CurrentOffset;
    ULONG ReadLength;
    ULONG AlreadyRead;

    if (!Context->Record.IsNonResident)
    {
        if (Offset > Context->Record.Resident.ValueLength)
            return 0;
        if (Offset + Length > Context->Record.Resident.ValueLength)
            Length = (ULONG)(Context->Record.Resident.ValueLength - Offset);
        RtlCopyMemory(Buffer, (PCHAR)&Context->Record + Context->Record.Resident.ValueOffset + Offset, Length);
        return Length;
    }

    /*
     * Non-resident attribute
     */

    /*
     * I. Find the corresponding start data run.
     */

    AlreadyRead = 0;

    // FIXME: Cache seems to be non-working. Disable it for now
    //if(Context->CacheRunOffset <= Offset && Offset < Context->CacheRunOffset + Context->CacheRunLength * Volume->ClusterSize)
    if (0)
    {
        DataRun = Context->CacheRun;
        LastLCN = Context->CacheRunLastLCN;
        DataRunStartLCN = Context->CacheRunStartLCN;
        DataRunLength = Context->CacheRunLength;
        CurrentOffset = Context->CacheRunCurrentOffset;
    }
    else
    {
        LastLCN = 0;
        DataRun = (PUCHAR)&Context->Record + Context->Record.NonResident.MappingPairsOffset;
        CurrentOffset = 0;

        while (1)
        {
            DataRun = NtfsDecodeRun(DataRun, &DataRunOffset, &DataRunLength);
            if (DataRunOffset != -1)
            {
                /* Normal data run. */
                DataRunStartLCN = LastLCN + DataRunOffset;
                LastLCN = DataRunStartLCN;
            }
            else
            {
                /* Sparse data run. */
                DataRunStartLCN = -1;
            }

            if (Offset >= CurrentOffset &&
                Offset < CurrentOffset + (DataRunLength * Volume->ClusterSize))
            {
                break;
            }

            if (*DataRun == 0)
            {
                return AlreadyRead;
            }

            CurrentOffset += DataRunLength * Volume->ClusterSize;
        }
    }

    /*
     * II. Go through the run list and read the data
     */

    ReadLength = (ULONG)min(DataRunLength * Volume->ClusterSize - (Offset - CurrentOffset), Length);
    if (DataRunStartLCN == -1)
        RtlZeroMemory(Buffer, ReadLength);
    if (DataRunStartLCN == -1 || NtfsDiskRead(Volume, DataRunStartLCN * Volume->ClusterSize + Offset - CurrentOffset, ReadLength, Buffer))
    {
        Length -= ReadLength;
        Buffer += ReadLength;
        AlreadyRead += ReadLength;

        if (ReadLength == DataRunLength * Volume->ClusterSize - (Offset - CurrentOffset))
        {
            CurrentOffset += DataRunLength * Volume->ClusterSize;
            DataRun = NtfsDecodeRun(DataRun, &DataRunOffset, &DataRunLength);
            if (DataRunOffset != (ULONGLONG)-1)
            {
                DataRunStartLCN = LastLCN + DataRunOffset;
                LastLCN = DataRunStartLCN;
            }
            else
                DataRunStartLCN = -1;
        }

        while (Length > 0)
        {
            ReadLength = (ULONG)min(DataRunLength * Volume->ClusterSize, Length);
            if (DataRunStartLCN == -1)
                RtlZeroMemory(Buffer, ReadLength);
            else if (!NtfsDiskRead(Volume, DataRunStartLCN * Volume->ClusterSize, ReadLength, Buffer))
                break;

            Length -= ReadLength;
            Buffer += ReadLength;
            AlreadyRead += ReadLength;

            /* We finished this request, but there still data in this data run. */
            if (Length == 0 && ReadLength != DataRunLength * Volume->ClusterSize)
                break;

            /*
             * Go to next run in the list.
             */

            if (*DataRun == 0)
                break;
            CurrentOffset += DataRunLength * Volume->ClusterSize;
            DataRun = NtfsDecodeRun(DataRun, &DataRunOffset, &DataRunLength);
            if (DataRunOffset != -1)
            {
                /* Normal data run. */
                DataRunStartLCN = LastLCN + DataRunOffset;
                LastLCN = DataRunStartLCN;
            }
            else
            {
                /* Sparse data run. */
                DataRunStartLCN = -1;
            }
        } /* while */

    } /* if Disk */

    Context->CacheRun = DataRun;
    Context->CacheRunOffset = Offset + AlreadyRead;
    Context->CacheRunStartLCN = DataRunStartLCN;
    Context->CacheRunLength = DataRunLength;
    Context->CacheRunLastLCN = LastLCN;
    Context->CacheRunCurrentOffset = CurrentOffset;

    return AlreadyRead;
}

static PNTFS_ATTR_CONTEXT NtfsFindAttributeHelper(
    PNTFS_VOLUME_INFO Volume,
    ULONGLONG CurrentMftIndex,
    PNTFS_ATTR_RECORD AttrRecord,
    PNTFS_ATTR_RECORD AttrRecordEnd,
    ULONG Type,
    const WCHAR *Name,
    ULONG NameLength,
    ULONG RecursionLimit,
    ULONG Instance);
static BOOLEAN NtfsReadMftRecord(PNTFS_VOLUME_INFO Volume, ULONGLONG MFTIndex, PNTFS_MFT_RECORD Buffer);

static PNTFS_ATTR_CONTEXT NtfsFindAttributeHelperList(
    PNTFS_VOLUME_INFO Volume,
    ULONGLONG ParentMftIndex,
    PNTFS_ATTR_LIST_ATTR AttrListRecord,
    PNTFS_ATTR_LIST_ATTR AttrListRecordEnd,
    ULONG Type,
    const WCHAR *Name,
    ULONG NameLength,
    ULONG RecursionLimit)
{
    ULONGLONG PrevMftIndex = -1;
    PNTFS_ATTR_CONTEXT Context = NULL;
    PNTFS_MFT_RECORD MftRecord;
    if (RecursionLimit < 1)
        return NULL;

    MftRecord = FwAllocate(Volume->MftRecordSize);
    if (!MftRecord)
        return NULL;

    while (AttrListRecord < AttrListRecordEnd)
    {
        ULONGLONG MftIndex = AttrListRecord->BaseFileRef & NTFS_MFT_MASK;
        ULONG AttrType = AttrListRecord->Type;
        ULONG AttrId = AttrListRecord->AttrId;

        if (AttrType == NTFS_ATTR_TYPE_END)
            break;

        if (MftIndex == ParentMftIndex)
        {
            goto skip;
        }

        if (AttrType == Type &&
            AttrListRecord->NameLength == NameLength)
        {
            PWCHAR AttrListName;

            AttrListName = (PWCHAR)((PCHAR)AttrListRecord + AttrListRecord->NameOffset);
            if (RtlEqualMemory(AttrListName, Name, NameLength * sizeof(WCHAR)))
            {
                PNTFS_ATTR_RECORD AttrRecord;
                PNTFS_ATTR_RECORD AttrRecordEnd;

                if (PrevMftIndex != MftIndex)
                {
                    PrevMftIndex = MftIndex;
                    if (!NtfsReadMftRecord(Volume, MftIndex, MftRecord))
                        goto skip;
                }

                AttrRecord = (PNTFS_ATTR_RECORD)((PCHAR)MftRecord + MftRecord->AttributesOffset);
                AttrRecordEnd = (PNTFS_ATTR_RECORD)((PCHAR)MftRecord + Volume->MftRecordSize);

                Context = NtfsFindAttributeHelper(Volume, MftIndex,
                                                  AttrRecord, AttrRecordEnd,
                                                  Type, Name, NameLength,
                                                  RecursionLimit - 1, AttrId);
                if (Context)
                    break;
            }
        }

skip:
        if (AttrListRecord->RecLength == 0)
            break;
        AttrListRecord = (PNTFS_ATTR_LIST_ATTR)((PCHAR)AttrListRecord + AttrListRecord->RecLength);
    }

    if (MftRecord)
        FwFree(MftRecord);

    return Context;
}

static PNTFS_ATTR_CONTEXT NtfsFindAttributeHelper(
    PNTFS_VOLUME_INFO Volume,
    ULONGLONG CurrentMftIndex,
    PNTFS_ATTR_RECORD AttrRecord,
    PNTFS_ATTR_RECORD AttrRecordEnd,
    ULONG Type,
    const WCHAR *Name,
    ULONG NameLength,
    ULONG RecursionLimit,
    ULONG Instance)
{
    PNTFS_ATTR_CONTEXT Context = NULL;
    PNTFS_ATTR_CONTEXT ListContext = NULL;
    PVOID ListBuffer = NULL;
    ULONGLONG ListSize = 0;
    if (RecursionLimit < 1)
        return NULL;

    while (AttrRecord < AttrRecordEnd)
    {
        ULONG AttrType = AttrRecord->Type;
        ULONG AttrInstance = AttrRecord->Instance;

        if (AttrType == NTFS_ATTR_TYPE_END)
            break;

        if (ListContext)
            NtfsReleaseAttributeContext(ListContext);
        if (ListBuffer)
            FwFree(ListBuffer);

        ListContext = ListBuffer = NULL;

        /* Limit the $ATTRIBUTE_LIST recursion or else infinity loop */
        if (AttrType != Type && AttrType == NTFS_ATTR_TYPE_ATTRIBUTE_LIST && RecursionLimit >= 2)
        {
            PNTFS_ATTR_LIST_ATTR ListAttrRecord;
            PNTFS_ATTR_LIST_ATTR ListAttrRecordEnd;

            ListContext = NtfsPrepareAttributeContext(AttrRecord);

            ListSize = NtfsGetAttributeSize(&ListContext->Record);
            if (ListSize <= 0xFFFFFFFF)
                ListBuffer = FwAllocate((ULONG)ListSize);
            else
                ListBuffer = NULL;

            if (!ListBuffer)
            {
                goto skip;
            }

            ListAttrRecord = (PNTFS_ATTR_LIST_ATTR)ListBuffer;
            ListAttrRecordEnd = (PNTFS_ATTR_LIST_ATTR)((PCHAR)ListBuffer + ListSize);

            if (NtfsReadAttribute(Volume, ListContext, 0, ListBuffer, (ULONG)ListSize) == ListSize)
            {
                Context = NtfsFindAttributeHelperList(Volume, CurrentMftIndex,
                                                      ListAttrRecord, ListAttrRecordEnd,
                                                      Type, Name, NameLength,
                                                      RecursionLimit - 1);

                if (Context != NULL)
                    break;
            }
        }

        if (AttrType == Type &&
            AttrRecord->NameLength == NameLength &&
            /* HACK for ntfs3 driver on linux! Because the ntfs3 likes to generate invalid
             * attribute size when mft record increases and I don't know how I will handle this */
            NtfsGetAttributeSize(AttrRecord) != 0 &&
            (Instance == (ULONG)-1 || AttrInstance == Instance))
        {
            PWCHAR AttrName;

            AttrName = (PWCHAR)((PCHAR)AttrRecord + AttrRecord->NameOffset);
            if (RtlEqualMemory(AttrName, Name, NameLength * sizeof(WCHAR)))
            {
                /* Found it, fill up the context and return */
                Context = NtfsPrepareAttributeContext(AttrRecord);
                break;
            }
        }

skip:
        if (AttrRecord->Length == 0)
            break;
        AttrRecord = (PNTFS_ATTR_RECORD)((PCHAR)AttrRecord + AttrRecord->Length);
    }

    if (ListContext)
        NtfsReleaseAttributeContext(ListContext);
    if (ListBuffer)
        FwFree(ListBuffer);

    return Context;
}

static PUCHAR NtfsEncodeRun(PUCHAR DataRun, LONGLONG DataRunOffset, BOOLEAN Sparse, ULONGLONG DataRunLength)
{
    UCHAR OffsetSize = 0;
    UCHAR LengthSize = 0;
    ULONGLONG Value;
    UCHAR i;

    Value = DataRunLength;
    do
    {
        LengthSize++;
        Value >>= 8;
    } while (Value != 0);

    if (!Sparse)
    {
        OffsetSize = 1;
        while (OffsetSize < 8 &&
               (DataRunOffset < -(1LL << (OffsetSize * 8 - 1)) ||
                DataRunOffset >= (1LL << (OffsetSize * 8 - 1))))
        {
            OffsetSize++;
        }
    }

    *DataRun++ = (UCHAR)((OffsetSize << 4) | LengthSize);
    for (i = 0; i < LengthSize; i++)
        *DataRun++ = (UCHAR)(DataRunLength >> (i * 8));
    for (i = 0; i < OffsetSize; i++)
        *DataRun++ = (UCHAR)((ULONGLONG)DataRunOffset >> (i * 8));

    return DataRun;
}

static PNTFS_ATTR_CONTEXT NtfsAppendAttributeExtent(PNTFS_ATTR_CONTEXT Context, PNTFS_ATTR_RECORD Extent)
{
    PNTFS_ATTR_CONTEXT NewContext;
    PUCHAR DataRun;
    PUCHAR Source;
    PUCHAR SourceEnd;
    LONGLONG DataRunOffset;
    ULONGLONG DataRunLength;
    LONGLONG LastLCN = 0;
    LONGLONG ExtentLCN = 0;
    BOOLEAN Sparse;
    ULONG NewLength;

    if (Extent->NonResident.MappingPairsOffset > Extent->Length)
        return NULL;

    NewLength = Context->Record.Length +
                (Extent->Length - Extent->NonResident.MappingPairsOffset) * 9 + 1;
    NewContext = FwAllocate(FIELD_OFFSET(NTFS_ATTR_CONTEXT, Record) + NewLength);
    if (!NewContext)
        return NULL;

    RtlCopyMemory(NewContext, Context, FIELD_OFFSET(NTFS_ATTR_CONTEXT, Record) + Context->Record.Length);

    DataRun = (PUCHAR)&NewContext->Record + NewContext->Record.NonResident.MappingPairsOffset;
    NewContext->CacheRun = DataRun;
    while (*DataRun != 0)
    {
        Sparse = ((*DataRun >> 4) == 0);
        DataRun = NtfsDecodeRun(DataRun, &DataRunOffset, &DataRunLength);
        if (!Sparse)
            LastLCN += DataRunOffset;
    }

    Source = (PUCHAR)Extent + Extent->NonResident.MappingPairsOffset;
    SourceEnd = (PUCHAR)Extent + Extent->Length;
    while (Source < SourceEnd && *Source != 0)
    {
        Sparse = ((*Source >> 4) == 0);
        Source = NtfsDecodeRun(Source, &DataRunOffset, &DataRunLength);
        if (Sparse)
        {
            DataRun = NtfsEncodeRun(DataRun, 0, TRUE, DataRunLength);
        }
        else
        {
            ExtentLCN += DataRunOffset;
            DataRun = NtfsEncodeRun(DataRun, ExtentLCN - LastLCN, FALSE, DataRunLength);
            LastLCN = ExtentLCN;
        }
    }
    *DataRun++ = 0;

    NewContext->Record.Length = (ULONG)(DataRun - (PUCHAR)&NewContext->Record);
    NewContext->Record.NonResident.HighestVCN = Extent->NonResident.HighestVCN;

    return NewContext;
}

static PNTFS_ATTR_CONTEXT NtfsFindAttributeExtents(
    PNTFS_VOLUME_INFO Volume,
    PNTFS_MFT_RECORD MftRecord,
    ULONGLONG MftIndex,
    ULONG Type,
    const WCHAR *Name,
    ULONG NameLength)
{
    PNTFS_ATTR_RECORD AttrRecord;
    PNTFS_ATTR_RECORD AttrRecordEnd;
    PNTFS_ATTR_LIST_ATTR ListEntry;
    PNTFS_ATTR_LIST_ATTR ListEnd;
    PNTFS_ATTR_CONTEXT ListContext;
    PNTFS_ATTR_CONTEXT Context = NULL;
    PNTFS_ATTR_CONTEXT NewContext;
    PNTFS_MFT_RECORD ExtentRecord = NULL;
    PNTFS_MFT_RECORD Record;
    PVOID ListBuffer = NULL;
    ULONGLONG ListSize;
    ULONGLONG ExtentIndex = (ULONGLONG)-1;
    ULONGLONG Index;
    ULONG Count = 0;
    BOOLEAN Success = FALSE;

    if (!Volume->MFTContext)
        return NULL;

    AttrRecord = (PNTFS_ATTR_RECORD)((PCHAR)MftRecord + MftRecord->AttributesOffset);
    AttrRecordEnd = (PNTFS_ATTR_RECORD)((PCHAR)MftRecord + Volume->MftRecordSize);
    while (AttrRecord < AttrRecordEnd &&
           AttrRecord->Type != NTFS_ATTR_TYPE_END &&
           AttrRecord->Type != NTFS_ATTR_TYPE_ATTRIBUTE_LIST)
    {
        if (AttrRecord->Length == 0)
            return NULL;
        AttrRecord = (PNTFS_ATTR_RECORD)((PCHAR)AttrRecord + AttrRecord->Length);
    }
    if (AttrRecord >= AttrRecordEnd || AttrRecord->Type != NTFS_ATTR_TYPE_ATTRIBUTE_LIST)
        return NULL;

    ListContext = NtfsPrepareAttributeContext(AttrRecord);
    ListSize = NtfsGetAttributeSize(&ListContext->Record);
    if (ListSize != 0 && ListSize <= 0xFFFFFFFF)
        ListBuffer = FwAllocate((ULONG)ListSize);
    if (!ListBuffer ||
        NtfsReadAttribute(Volume, ListContext, 0, ListBuffer, (ULONG)ListSize) != ListSize)
    {
        goto Cleanup;
    }

    ExtentRecord = FwAllocate(Volume->MftRecordSize);
    if (!ExtentRecord)
        goto Cleanup;

    ListEntry = (PNTFS_ATTR_LIST_ATTR)ListBuffer;
    ListEnd = (PNTFS_ATTR_LIST_ATTR)((PCHAR)ListBuffer + ListSize);
    while (ListEntry < ListEnd && ListEntry->Type != NTFS_ATTR_TYPE_END && ListEntry->RecLength != 0)
    {
        if (ListEntry->Type == Type &&
            ListEntry->NameLength == NameLength &&
            RtlEqualMemory((PCHAR)ListEntry + ListEntry->NameOffset, Name, NameLength * sizeof(WCHAR)))
        {
            Index = ListEntry->BaseFileRef & NTFS_MFT_MASK;
            if (Index == MftIndex)
            {
                Record = MftRecord;
            }
            else
            {
                if (Index != ExtentIndex)
                {
                    ExtentIndex = (ULONGLONG)-1;
                    if (!NtfsReadMftRecord(Volume, Index, ExtentRecord))
                        goto Cleanup;
                    ExtentIndex = Index;
                }
                Record = ExtentRecord;
            }

            AttrRecord = (PNTFS_ATTR_RECORD)((PCHAR)Record + Record->AttributesOffset);
            AttrRecordEnd = (PNTFS_ATTR_RECORD)((PCHAR)Record + Volume->MftRecordSize);
            while (AttrRecord < AttrRecordEnd &&
                   AttrRecord->Type != NTFS_ATTR_TYPE_END &&
                   (AttrRecord->Type != Type || AttrRecord->Instance != ListEntry->AttrId))
            {
                if (AttrRecord->Length == 0)
                    goto Cleanup;
                AttrRecord = (PNTFS_ATTR_RECORD)((PCHAR)AttrRecord + AttrRecord->Length);
            }
            if (AttrRecord >= AttrRecordEnd || AttrRecord->Type != Type || !AttrRecord->IsNonResident)
                goto Cleanup;

            if (!Context)
            {
                if (AttrRecord->NonResident.LowestVCN != 0)
                    goto Cleanup;
                Context = NtfsPrepareAttributeContext(AttrRecord);
            }
            else
            {
                if (AttrRecord->NonResident.LowestVCN != Context->Record.NonResident.HighestVCN + 1)
                    goto Cleanup;
                NewContext = NtfsAppendAttributeExtent(Context, AttrRecord);
                if (!NewContext)
                    goto Cleanup;
                NtfsReleaseAttributeContext(Context);
                Context = NewContext;
            }
            Count++;
        }

        ListEntry = (PNTFS_ATTR_LIST_ATTR)((PCHAR)ListEntry + ListEntry->RecLength);
    }

    Success = (Count > 1);

Cleanup:
    if (!Success && Context)
    {
        NtfsReleaseAttributeContext(Context);
        Context = NULL;
    }
    if (ExtentRecord)
        FwFree(ExtentRecord);
    if (ListBuffer)
        FwFree(ListBuffer);
    NtfsReleaseAttributeContext(ListContext);

    return Context;
}

static PNTFS_ATTR_CONTEXT NtfsFindAttribute(PNTFS_VOLUME_INFO Volume, PNTFS_MFT_RECORD MftRecord, ULONGLONG MftIndex, ULONG Type, const WCHAR *Name)
{
    PNTFS_ATTR_RECORD AttrRecord;
    PNTFS_ATTR_RECORD AttrRecordEnd;
    PNTFS_ATTR_CONTEXT Context;
    ULONG NameLength;

    AttrRecord = (PNTFS_ATTR_RECORD)((PCHAR)MftRecord + MftRecord->AttributesOffset);
    AttrRecordEnd = (PNTFS_ATTR_RECORD)((PCHAR)MftRecord + Volume->MftRecordSize);
    for (NameLength = 0; Name[NameLength] != 0; NameLength++)
        ;

    Context = NtfsFindAttributeExtents(Volume, MftRecord, MftIndex, Type, Name, NameLength);
    if (Context)
        return Context;

    return NtfsFindAttributeHelper(Volume, MftIndex, AttrRecord, AttrRecordEnd, Type, Name, NameLength, NTFS_MAX_ATTRIBUTE_LIST_RECURSION, -1);
}

static BOOLEAN NtfsFixupRecord(PNTFS_VOLUME_INFO Volume, PNTFS_RECORD Record)
{
    USHORT *USA;
    USHORT USANumber;
    USHORT USACount;
    USHORT *Block;

    USA = (USHORT*)((PCHAR)Record + Record->USAOffset);
    USANumber = *(USA++);
    USACount = Record->USACount - 1; /* Exclude the USA Number. */
    Block = (USHORT*)((PCHAR)Record + Volume->BootSector.BytesPerSector - 2);

    while (USACount)
    {
        if (*Block != USANumber)
            return FALSE;
        *Block = *(USA++);
        Block = (USHORT*)((PCHAR)Block + Volume->BootSector.BytesPerSector);
        USACount--;
    }

    return TRUE;
}

static BOOLEAN NtfsReadMftRecord(PNTFS_VOLUME_INFO Volume, ULONGLONG MFTIndex, PNTFS_MFT_RECORD Buffer)
{
    ULONGLONG BytesRead;

    BytesRead = NtfsReadAttribute(Volume, Volume->MFTContext, MFTIndex * Volume->MftRecordSize, (PCHAR)Buffer, Volume->MftRecordSize);
    if (BytesRead != Volume->MftRecordSize)
        return FALSE;

    /* Apply update sequence array fixups. */
    return NtfsFixupRecord(Volume, (PNTFS_RECORD)Buffer);
}

static BOOLEAN
NtfsCompareFileName(
    _In_ PCCH FileName,
    _In_ SIZE_T FileNameLen,
    _In_ PNTFS_INDEX_ENTRY IndexEntry)
{
    PWCHAR EntryFileName;
    UCHAR EntryFileNameLength;
    UCHAR i;

    EntryFileName = IndexEntry->FileName.FileName;
    EntryFileNameLength = IndexEntry->FileName.FileNameLength;

    if (FileNameLen != EntryFileNameLength)
        return FALSE;

    /*
     * Always perform case-insensitive comparison for file names.
     * This is necessary, because when modifying e.g. on Linux a Windows NTFS
     * partition formatted with Windows itself, the NTLDR/BOOTMGR will boot
     * normally ignoring the case of the paths.
     */
    for (i = 0; i < EntryFileNameLength; i++)
    {
        if (tolower(EntryFileName[i]) != tolower(FileName[i]))
            return FALSE;
    }

    return TRUE;
}

static BOOLEAN
NtfsFindMftRecord(
    _In_ PNTFS_VOLUME_INFO Volume,
    _In_ ULONGLONG MFTIndex,
    _In_ PCSTR FileName,
    _Out_ PULONGLONG OutMFTIndex,
    _Out_ PULONG FileAttributes)
{
    PNTFS_MFT_RECORD MftRecord;
    //ULONG Magic;
    PNTFS_ATTR_CONTEXT IndexRootCtx;
    PNTFS_ATTR_CONTEXT IndexBitmapCtx;
    PNTFS_ATTR_CONTEXT IndexAllocationCtx;
    PNTFS_INDEX_ROOT IndexRoot;
    ULONGLONG BitmapDataSize;
    ULONGLONG IndexAllocationSize;
    PCHAR BitmapData;
    PCHAR IndexRecord;
    PNTFS_INDEX_ENTRY IndexEntry, IndexEntryEnd;
    ULONG RecordOffset;
    ULONG IndexBlockSize;
    SIZE_T FileNameLen;

    FileNameLen = strlen(FileName);

    MftRecord = FwAllocate(Volume->MftRecordSize);
    if (MftRecord == NULL)
    {
        return FALSE;
    }

    if (NtfsReadMftRecord(Volume, MFTIndex, MftRecord))
    {
        //Magic = MftRecord->Magic;

        IndexRootCtx = NtfsFindAttribute(Volume, MftRecord, MFTIndex, NTFS_ATTR_TYPE_INDEX_ROOT, L"$I30");
        if (IndexRootCtx == NULL)
        {
            FwFree(MftRecord);
            return FALSE;
        }

        IndexRecord = FwAllocate(Volume->IndexRecordSize);
        if (IndexRecord == NULL)
        {
            FwFree(MftRecord);
            return FALSE;
        }

        NtfsReadAttribute(Volume, IndexRootCtx, 0, IndexRecord, Volume->IndexRecordSize);
        IndexRoot = (PNTFS_INDEX_ROOT)IndexRecord;
        IndexEntry = (PNTFS_INDEX_ENTRY)((PCHAR)&IndexRoot->IndexHeader + IndexRoot->IndexHeader.EntriesOffset);
        /* Index root is always resident. */
        IndexEntryEnd = (PNTFS_INDEX_ENTRY)(IndexRecord + IndexRootCtx->Record.Resident.ValueLength);
        NtfsReleaseAttributeContext(IndexRootCtx);

        while (IndexEntry < IndexEntryEnd &&
               !(IndexEntry->Flags & NTFS_INDEX_ENTRY_END))
        {
            if (NtfsCompareFileName(FileName, FileNameLen, IndexEntry))
            {
                *OutMFTIndex = (IndexEntry->Data.Directory.IndexedFile & NTFS_MFT_MASK);
                *FileAttributes = IndexEntry->FileName.FileAttributes;
                FwFree(IndexRecord);
                FwFree(MftRecord);
                return TRUE;
            }
            IndexEntry = (PNTFS_INDEX_ENTRY)((PCHAR)IndexEntry + IndexEntry->Length);
        }

        if (IndexRoot->IndexHeader.Flags & NTFS_LARGE_INDEX)
        {
            IndexBlockSize = IndexRoot->IndexBlockSize;

            IndexBitmapCtx = NtfsFindAttribute(Volume, MftRecord, MFTIndex, NTFS_ATTR_TYPE_BITMAP, L"$I30");
            if (IndexBitmapCtx == NULL)
            {
                FwFree(MftRecord);
                return FALSE;
            }
            BitmapDataSize = NtfsGetAttributeSize(&IndexBitmapCtx->Record);
            if(BitmapDataSize <= 0xFFFFFFFF)
                BitmapData = FwAllocate((ULONG)BitmapDataSize);
            else
                BitmapData = NULL;

            if (BitmapData == NULL)
            {
                FwFree(IndexRecord);
                FwFree(MftRecord);
                return FALSE;
            }
            NtfsReadAttribute(Volume, IndexBitmapCtx, 0, BitmapData, (ULONG)BitmapDataSize);
            NtfsReleaseAttributeContext(IndexBitmapCtx);

            IndexAllocationCtx = NtfsFindAttribute(Volume, MftRecord, MFTIndex, NTFS_ATTR_TYPE_INDEX_ALLOCATION, L"$I30");
            if (IndexAllocationCtx == NULL)
            {
                FwFree(BitmapData);
                FwFree(IndexRecord);
                FwFree(MftRecord);
                return FALSE;
            }
            IndexAllocationSize = NtfsGetAttributeSize(&IndexAllocationCtx->Record);

            RecordOffset = 0;

            for (;;)
            {
                for (; RecordOffset < IndexAllocationSize;)
                {
                    UCHAR Bit = 1 << ((RecordOffset / IndexBlockSize) & 7);
                    ULONG Byte = (RecordOffset / IndexBlockSize) >> 3;
                    if ((BitmapData[Byte] & Bit))
                        break;
                    RecordOffset += IndexBlockSize;
                }

                if (RecordOffset >= IndexAllocationSize)
                {
                    break;
                }

                NtfsReadAttribute(Volume, IndexAllocationCtx, RecordOffset, IndexRecord, IndexBlockSize);

                if (!NtfsFixupRecord(Volume, (PNTFS_RECORD)IndexRecord))
                {
                    break;
                }

                /* FIXME */
                IndexEntry = (PNTFS_INDEX_ENTRY)(IndexRecord + 0x18 + *(USHORT *)(IndexRecord + 0x18));
                IndexEntryEnd = (PNTFS_INDEX_ENTRY)(IndexRecord + IndexBlockSize);

                while (IndexEntry < IndexEntryEnd &&
                       !(IndexEntry->Flags & NTFS_INDEX_ENTRY_END))
                {
                    if (NtfsCompareFileName(FileName, FileNameLen, IndexEntry))
                    {
                        *OutMFTIndex = (IndexEntry->Data.Directory.IndexedFile & NTFS_MFT_MASK);
                        *FileAttributes = IndexEntry->FileName.FileAttributes;
                        NtfsReleaseAttributeContext(IndexAllocationCtx);
                        FwFree(BitmapData);
                        FwFree(IndexRecord);
                        FwFree(MftRecord);
                        return TRUE;
                    }
                    IndexEntry = (PNTFS_INDEX_ENTRY)((PCHAR)IndexEntry + IndexEntry->Length);
                }

                RecordOffset += IndexBlockSize;
            }

            NtfsReleaseAttributeContext(IndexAllocationCtx);
            FwFree(BitmapData);
        }

        FwFree(IndexRecord);
    }
    FwFree(MftRecord);

    return FALSE;
}

static ULONG NtfsGetNumPathParts(PCSTR Path)
{
    size_t i;
    size_t len;
    ULONG  num;

    len = strlen(Path);

    for (i = 0, num = 0; i < len; i++)
    {
        if ((Path[i] == '\\') || (Path[i] == '/'))
        {
            num++;
        }
    }
    num++;

    return num;
}

static VOID NtfsGetFirstNameFromPath(PCHAR Buffer, PCSTR Path)
{
    size_t i;
    size_t len;

    len = strlen(Path);

    for (i = 0; i < len; i++)
    {
        if ((Path[i] == '\\') || (Path[i] == '/'))
        {
            break;
        }
        else
        {
            Buffer[i] = Path[i];
        }
    }

    Buffer[i] = 0;
}

static BOOLEAN NtfsLookupFile(PNTFS_VOLUME_INFO Volume, PCSTR FileName, PNTFS_MFT_RECORD MftRecord, PNTFS_FILE_HANDLE FileHandle)
{
    ULONG NumberOfPathParts;
    ULONG i;
    ULONGLONG CurrentMFTIndex;
    ULONG FileAttributes = NTFS_FILE_ATTR_DIRECTORY;
    CHAR PathPart[261];

    CurrentMFTIndex = NTFS_FILE_ROOT;

    /* Skip leading path separator, if any */
    if (*FileName == '\\' || *FileName == '/')
        ++FileName;
    PathPart[0] = ANSI_NULL;

    /* Figure out how many sub-directories we are nested in and loop once for each part */
    NumberOfPathParts = NtfsGetNumPathParts(FileName);
    for (i = 0; i < NumberOfPathParts; i++)
    {
        NtfsGetFirstNameFromPath(PathPart, FileName);

        for (; (*FileName != '\\') && (*FileName != '/') && (*FileName != '\0'); FileName++)
            ;
        FileName++;

        if (!NtfsFindMftRecord(Volume, CurrentMFTIndex, PathPart, &CurrentMFTIndex, &FileAttributes))
        {
            return FALSE;
        }
    }

    if (!NtfsReadMftRecord(Volume, CurrentMFTIndex, MftRecord))
    {
        return FALSE;
    }

    FileHandle->DataContext = NtfsFindAttribute(Volume, MftRecord, CurrentMFTIndex, NTFS_ATTR_TYPE_DATA, L"");
    if (FileHandle->DataContext == NULL)
    {
        return FALSE;
    }

    return TRUE;
}

PVOID
NtfsReadFile(
    _In_ PNTFS_VOLUME_INFO Volume,
    _In_ PCSTR Path,
    _Out_ PULONG Size)
{
    PNTFS_FILE_HANDLE FileHandle;
    PNTFS_MFT_RECORD MftRecord;
    ULONGLONG FileSize;
    PVOID Data = NULL;

    *Size = 0;

    FileHandle = FwAllocate(sizeof(NTFS_FILE_HANDLE) + Volume->MftRecordSize);
    if (!FileHandle)
        return NULL;
    RtlZeroMemory(FileHandle, sizeof(NTFS_FILE_HANDLE) + Volume->MftRecordSize);
    FileHandle->Volume = Volume;

    MftRecord = (PNTFS_MFT_RECORD)(FileHandle + 1);
    if (!NtfsLookupFile(Volume, Path, MftRecord, FileHandle))
    {
        FwFree(FileHandle);
        return NULL;
    }

    FileSize = NtfsGetAttributeSize(&FileHandle->DataContext->Record);
    if (FileSize != 0 &&
        FileSize <= MAXULONG &&
        !(FileHandle->DataContext->Record.Flags & NTFS_ATTR_COMPRESSED))
    {
        Data = FwAllocatePages((SIZE_T)FileSize);
        if (Data &&
            NtfsReadAttribute(Volume, FileHandle->DataContext, 0, Data, (ULONG)FileSize) != FileSize)
        {
            FwFreePages(Data, (SIZE_T)FileSize);
            Data = NULL;
        }
    }

    if (Data)
        *Size = (ULONG)FileSize;

    NtfsReleaseAttributeContext(FileHandle->DataContext);
    FwFree(FileHandle);
    return Data;
}

PNTFS_FILE_HANDLE
NtfsOpenFile(
    _In_ PNTFS_VOLUME_INFO Volume,
    _In_ PCSTR Path,
    _Out_ PULONGLONG Size)
{
    PNTFS_FILE_HANDLE FileHandle;
    PNTFS_MFT_RECORD MftRecord;

    *Size = 0;

    FileHandle = FwAllocate(sizeof(NTFS_FILE_HANDLE) + Volume->MftRecordSize);
    if (!FileHandle)
        return NULL;
    RtlZeroMemory(FileHandle, sizeof(NTFS_FILE_HANDLE) + Volume->MftRecordSize);
    FileHandle->Volume = Volume;

    MftRecord = (PNTFS_MFT_RECORD)(FileHandle + 1);
    if (!NtfsLookupFile(Volume, Path, MftRecord, FileHandle))
    {
        FwFree(FileHandle);
        return NULL;
    }

    if (FileHandle->DataContext->Record.Flags & NTFS_ATTR_COMPRESSED)
    {
        NtfsReleaseAttributeContext(FileHandle->DataContext);
        FwFree(FileHandle);
        return NULL;
    }

    *Size = NtfsGetAttributeSize(&FileHandle->DataContext->Record);
    return FileHandle;
}

BOOLEAN
NtfsReadFileAt(
    _In_ PNTFS_FILE_HANDLE FileHandle,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    return NtfsReadAttribute(FileHandle->Volume, FileHandle->DataContext, Offset, Buffer, Length) == Length;
}

VOID
NtfsCloseFile(
    _In_ PNTFS_FILE_HANDLE FileHandle)
{
    NtfsReleaseAttributeContext(FileHandle->DataContext);
    FwFree(FileHandle);
}

PNTFS_VOLUME_INFO
NtfsMount(
    _In_ PDK_DEVICE Device)
{
    PNTFS_VOLUME_INFO Volume;

    Volume = FwAllocate(sizeof(NTFS_VOLUME_INFO));
    if (!Volume)
        return NULL;
    RtlZeroMemory(Volume, sizeof(NTFS_VOLUME_INFO));
    Volume->Device = *Device;

    if (!DkRead(Device, 0, sizeof(Volume->BootSector), &Volume->BootSector) ||
        !RtlEqualMemory(Volume->BootSector.SystemId, "NTFS", 4) ||
        Volume->BootSector.BytesPerSector == 0 ||
        Volume->BootSector.SectorsPerCluster == 0)
    {
        FwFree(Volume);
        return NULL;
    }

    Volume->ClusterSize = Volume->BootSector.SectorsPerCluster * Volume->BootSector.BytesPerSector;
    if (Volume->BootSector.ClustersPerMftRecord > 0)
        Volume->MftRecordSize = Volume->BootSector.ClustersPerMftRecord * Volume->ClusterSize;
    else
        Volume->MftRecordSize = 1 << (-Volume->BootSector.ClustersPerMftRecord);
    if (Volume->BootSector.ClustersPerIndexRecord > 0)
        Volume->IndexRecordSize = Volume->BootSector.ClustersPerIndexRecord * Volume->ClusterSize;
    else
        Volume->IndexRecordSize = 1 << (-Volume->BootSector.ClustersPerIndexRecord);

    Volume->MasterFileTable = FwAllocate(Volume->MftRecordSize);
    if (!Volume->MasterFileTable)
    {
        FwFree(Volume);
        return NULL;
    }

    if (!NtfsDiskRead(Volume,
                      Volume->BootSector.MftLocation * Volume->ClusterSize,
                      Volume->MftRecordSize,
                      (PCHAR)Volume->MasterFileTable) ||
        !NtfsFixupRecord(Volume, (PNTFS_RECORD)Volume->MasterFileTable))
    {
        FwFree(Volume->MasterFileTable);
        FwFree(Volume);
        return NULL;
    }

    Volume->MFTContext = NtfsFindAttribute(Volume, Volume->MasterFileTable, 0, NTFS_ATTR_TYPE_DATA, L"");
    if (!Volume->MFTContext)
    {
        FwFree(Volume->MasterFileTable);
        FwFree(Volume);
        return NULL;
    }

    return Volume;
}
