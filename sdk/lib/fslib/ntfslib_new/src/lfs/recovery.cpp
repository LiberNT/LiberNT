/*
 * PROJECT:     LiberNT NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Restart recovery from an LFS 2.0 $LogFile
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

namespace
{

const ULONG LfsBufferPageCount = 32;
const ULONG LfsBufferHalf = 16;
const ULONG LfsFirstBufferPage = 2;
const ULONG LfsPageRecordEnd = 0x00000001;
const ULONG LfsClientRecord = 1;
const ULONG LfsClientRestart = 2;
const USHORT LfsNoClient = 0xffff;
const ULONG RestartEntryAllocated = 0xffffffff;
const ULONG NtfsRestartMajorVersion = 1;
const ULONG LogBlockSize = 512;
const ULONG MaximumCachedBytes = 128 * 1024 * 1024;
const ULONG ResetChunkSize = 0x10000;
const LONG SourceHome = -1;
const ULONGLONG NoLsn = ~0ULL;
const ULONG ResidentHeaderLength = FIELD_OFFSET(Attribute, Resident.Padding) + sizeof(UINT8);
const ULONG NonResidentHeaderLength = FIELD_OFFSET(Attribute, NonResident.CompressedDataSize);
const USHORT VolumeIsDirty = 0x0001;
const ULONG VolumeRecordNumber = 3;
const ULONG MaximumRecordClusters = 8;

struct LcnSlot
{
    ULONGLONG Lcn;
    ULONGLONG OldestLsn;
    PUCHAR Data;
    PUCHAR RedoData;
    BOOLEAN Used;
    BOOLEAN Dirty;
    BOOLEAN UndoDirty;
};

class LcnMap
{
public:
    LcnSlot* Slots = NULL;
    ULONG Capacity = 0;
    ULONG Count = 0;

    ~LcnMap()
    {
        for (ULONG Index = 0; Index < Capacity; Index++)
        {
            delete[] Slots[Index].Data;
            delete[] Slots[Index].RedoData;
        }
        delete[] Slots;
    }

    LcnSlot* Find(_In_ ULONGLONG Lcn) const
    {
        if (!Capacity)
            return NULL;

        for (ULONG Index = Hash(Lcn, Capacity);; Index = (Index + 1) & (Capacity - 1))
        {
            if (!Slots[Index].Used)
                return NULL;
            if (Slots[Index].Lcn == Lcn)
                return &Slots[Index];
        }
    }

    LcnSlot* Insert(_In_ ULONGLONG Lcn)
    {
        LcnSlot* Slot = Find(Lcn);

        if (Slot)
            return Slot;
        if ((Count + 1) * 2 > Capacity && !Grow())
            return NULL;

        Slot = Place(Slots, Capacity, Lcn);
        Slot->Lcn = Lcn;
        Slot->OldestLsn = NoLsn;
        Slot->Data = NULL;
        Slot->RedoData = NULL;
        Slot->Dirty = FALSE;
        Slot->UndoDirty = FALSE;
        Slot->Used = TRUE;
        Count++;
        return Slot;
    }

private:
    static ULONG Hash(_In_ ULONGLONG Lcn, _In_ ULONG Size)
    {
        return (ULONG)((Lcn * 0x9E3779B97F4A7C15ULL) >> 32) & (Size - 1);
    }

    static LcnSlot* Place(_In_ LcnSlot* Table, _In_ ULONG Size, _In_ ULONGLONG Lcn)
    {
        ULONG Index = Hash(Lcn, Size);

        while (Table[Index].Used)
            Index = (Index + 1) & (Size - 1);
        return &Table[Index];
    }

    BOOLEAN Grow()
    {
        ULONG NewCapacity = Capacity ? Capacity * 2 : 64;
        LcnSlot* NewSlots = new(PagedPool, TAG_LOG_FILE_SERVICE) LcnSlot[NewCapacity];

        if (!NewSlots)
            return FALSE;
        RtlZeroMemory(NewSlots, NewCapacity * sizeof(LcnSlot));
        for (ULONG Index = 0; Index < Capacity; Index++)
        {
            if (Slots[Index].Used)
                *Place(NewSlots, NewCapacity, Slots[Index].Lcn) = Slots[Index];
        }
        delete[] Slots;
        Slots = NewSlots;
        Capacity = NewCapacity;
        return TRUE;
    }
};

struct OpenAttributeInfo
{
    BOOLEAN Valid;
    ULONG Type;
    ULONG BytesPerIndexBuffer;
    ULONGLONG FileReference;
};

struct TransactionInfo
{
    BOOLEAN Active;
    ULONGLONG UndoNextLsn;
};

struct LogAction
{
    ULONGLONG Lsn;
    USHORT Operation;
    USHORT TargetAttribute;
    USHORT RecordOffset;
    USHORT AttributeOffset;
    USHORT ClusterBlockOffset;
    USHORT LcnCount;
    const ULONGLONG* Lcns;
    const UCHAR* Data;
    ULONG Length;
    ULONG OtherLength;
    BOOLEAN Undo;
};

class LogRecovery
{
public:
    LogRecovery(_In_ PVolume TargetVolume,
                _In_ PFileRecord LogFileRecord,
                _In_ PAttribute LogDataAttribute,
                _In_ PLfsRestartPage RestartPage,
                _In_ ULONGLONG LogStreamSize);
    ~LogRecovery();

    NTSTATUS Run(_Out_ PBOOLEAN Modified);
    NTSTATUS ResetLog();

private:
    PVolume DiskVolume;
    PFileRecord LogFile;
    PAttribute LogData;
    PLfsRestartPage Restart;
    ULONGLONG StreamSize;

    ULONG PageSize = 0;
    ULONG ClusterSize = 0;
    ULONG FileRecordSize = 0;
    ULONG IndexBufferSize = 0;
    ULONG OffsetBits = 0;
    ULONG RecordHeaderLength = 0;
    ULONG PageDataOffset = 0;
    ULONGLONG LogSize = 0;
    ULONGLONG FirstLogPage = 0;
    ULONGLONG CurrentLsn = 0;
    ULONGLONG ClientRestartLsn = 0;
    ULONGLONG MftLcn = 0;
    ULONGLONG MftMirrorLcn = 0;

    PUCHAR Buffers = NULL;
    BOOLEAN BufferValid[LfsBufferPageCount] = {};
    ULONGLONG BufferHome[LfsBufferPageCount] = {};
    PUCHAR HomePages = NULL;
    ULONGLONG HomeOffset[2] = { NoLsn, NoLsn };
    BOOLEAN HomeValid[2] = {};
    ULONG NextHomeSlot = 0;

    PUCHAR Record = NULL;
    ULONG RecordCapacity = 0;
    ULONG RecordLength = 0;
    PUCHAR Block = NULL;
    ULONG BlockCapacity = 0;

    LcnMap Clusters;
    ULONGLONG CachedBytes = 0;
    OpenAttributeInfo* OpenAttributes = NULL;
    ULONG OpenAttributeCount = 0;
    TransactionInfo* Transactions = NULL;
    ULONG TransactionCount = 0;
    ULONGLONG LastLsn = 0;
    ULONGLONG RedoStartLsn = NoLsn;
    ULONG AppliedCount = 0;
    ULONG UndoneCount = 0;

    ULONGLONG LsnToOffset(_In_ ULONGLONG Lsn) const
    {
        return (Lsn & ((1ULL << OffsetBits) - 1)) << 3;
    }

    ULONGLONG NextHome(_In_ ULONGLONG Home) const
    {
        Home += PageSize;
        return Home >= LogSize ? FirstLogPage : Home;
    }

    PLfsRecord RecordHeader() const
    {
        return reinterpret_cast<PLfsRecord>(Record);
    }

    NTSTATUS Prepare();
    BOOLEAN ReadLogPage(_In_ ULONGLONG Offset, _Out_writes_bytes_(PageSize) PUCHAR Page);
    PUCHAR HomePage(_In_ ULONGLONG Home);
    PUCHAR FindStartPage(_In_ ULONGLONG Lsn, _In_ ULONGLONG Home, _In_ ULONG InPage, _Out_ LONG* Source);
    BOOLEAN TailMatches(_In_ PUCHAR Page, _In_ ULONGLONG Home, _In_ ULONGLONG Lsn, _In_ ULONG End) const;
    PUCHAR FindContinuationPage(_In_ ULONGLONG Home,
                                _In_ ULONGLONG Lsn,
                                _In_ BOOLEAN Ends,
                                _In_ ULONG End,
                                _In_ LONG StartSource,
                                _In_ ULONG Index);
    NTSTATUS ReadRecord(_In_ ULONGLONG Lsn, _Out_ PULONGLONG NextLsn);
    NTSTATUS ReadClientRecord(_In_ ULONGLONG Lsn,
                              _Out_ PNtfsLogRecord* Client,
                              _Out_ PULONG ClientLength,
                              _Out_ PULONGLONG NextLsn);
    NTSTATUS ReadTable(_In_ ULONGLONG Lsn,
                       _In_ USHORT Operation,
                       _Out_ PRestartTable* Table,
                       _Out_ PULONG TableLength);

    OpenAttributeInfo* OpenAttribute(_In_ ULONG Index, _In_ BOOLEAN Create);
    TransactionInfo* Transaction(_In_ ULONG Index, _In_ BOOLEAN Create);
    NTSTATUS LoadRestartTables(_In_ PRestartArea Area);
    NTSTATUS Analyze();
    NTSTATUS Redo();
    NTSTATUS Undo();

    PUCHAR Cluster(_In_ ULONGLONG Lcn);
    NTSTATUS LoadBlock(_In_ const LogAction& Action, _In_ ULONG Offset, _In_ ULONG Size);
    NTSTATUS StoreBlock(_In_ const LogAction& Action, _In_ ULONG Offset, _In_ ULONG Size);
    NTSTATUS LoadStructure(_In_ const LogAction& Action,
                           _In_ const char* Signature,
                           _In_ ULONG Size,
                           _Out_ PBOOLEAN Valid);
    NTSTATUS StoreStructure(_In_ const LogAction& Action, _In_ ULONG Size, _In_ ULONGLONG Lsn);
    void Count(_In_ const LogAction& Action)
    {
        if (Action.Undo)
            UndoneCount++;
        else
            AppliedCount++;
    }
    NTSTATUS ResizeAttribute(_In_ ULONG AttributeStart, _In_ ULONG NewLength);
    NTSTATUS ApplyToFileRecord(_In_ const LogAction& Action);
    NTSTATUS ApplyToIndexBuffer(_In_ const LogAction& Action);
    NTSTATUS ApplyToClusters(_In_ const LogAction& Action);
    NTSTATUS Apply(_In_ const LogAction& Action);
    NTSTATUS SetVolumeDirty(_In_ BOOLEAN Dirty, _Inout_ PBOOLEAN WasDirty);
    NTSTATUS WriteClusters(_In_ BOOLEAN UndoState, _Inout_ PBOOLEAN Modified);
};

LogRecovery::LogRecovery(_In_ PVolume TargetVolume,
                         _In_ PFileRecord LogFileRecord,
                         _In_ PAttribute LogDataAttribute,
                         _In_ PLfsRestartPage RestartPage,
                         _In_ ULONGLONG LogStreamSize)
{
    DiskVolume = TargetVolume;
    LogFile = LogFileRecord;
    LogData = LogDataAttribute;
    Restart = RestartPage;
    StreamSize = LogStreamSize;
}

LogRecovery::~LogRecovery()
{
    delete[] Buffers;
    delete[] HomePages;
    delete[] Record;
    delete[] Block;
    delete[] OpenAttributes;
    delete[] Transactions;
}

BOOLEAN
LogRecovery::ReadLogPage(_In_ ULONGLONG Offset,
                         _Out_writes_bytes_(PageSize) PUCHAR Page)
{
    ULONG Remaining = PageSize;
    NTSTATUS Status;

    if (Offset >= StreamSize || PageSize > StreamSize - Offset)
        return FALSE;

    Status = LogFile->CopyData(LogData, Page, &Remaining, Offset);
    if (!NT_SUCCESS(Status) || Remaining != 0)
        return FALSE;
    if (RtlCompareMemory(Page, "RCRD", 4) != 4)
        return FALSE;

    return NT_SUCCESS(NtfsApplyFixup(reinterpret_cast<PNTFSRecordHeader>(Page),
                                     PageSize,
                                     DiskVolume->BytesPerSector));
}

NTSTATUS
LogRecovery::Prepare()
{
    PLfsRestartArea Area;
    PLfsClientRecord Client;
    BootSector* Boot;
    PUCHAR BootData;
    NTSTATUS Status;
    ULONG AreaOffset = Restart->RestartOffset;
    ULONG SeqNumberBits;

    PageSize = Restart->LogPageSize;
    if (PageSize != Restart->SystemPageSize ||
        PageSize < DiskVolume->BytesPerSector ||
        (PageSize & (PageSize - 1)) != 0)
    {
        return STATUS_FILE_SYSTEM_LIMITATION;
    }

    Area = reinterpret_cast<PLfsRestartArea>(reinterpret_cast<PUCHAR>(Restart) + AreaOffset);
    if (Area->ClientInUseList == LfsNoClient)
        return STATUS_SUCCESS;
    if (Area->ClientInUseList != 0 ||
        Area->ClientArrayOffset > PageSize - AreaOffset ||
        sizeof(LfsClientRecord) > PageSize - AreaOffset - Area->ClientArrayOffset)
    {
        return STATUS_FILE_SYSTEM_LIMITATION;
    }

    Client = reinterpret_cast<PLfsClientRecord>(reinterpret_cast<PUCHAR>(Area) + Area->ClientArrayOffset);
    SeqNumberBits = Area->SeqNumberBits;
    RecordHeaderLength = Area->RecordHeaderLength;
    PageDataOffset = Area->LogPageDataOffset;
    LogSize = Area->FileSize;
    CurrentLsn = Area->CurrentLsn;
    ClientRestartLsn = Client->ClientRestartLsn;
    FirstLogPage = (ULONGLONG)(LfsFirstBufferPage + LfsBufferPageCount) * PageSize;

    if (SeqNumberBits < 4 || SeqNumberBits > 60 ||
        RecordHeaderLength != FIELD_OFFSET(LfsRecord, Data) ||
        PageDataOffset < FIELD_OFFSET(LfsRecordPage, UpdateSequenceArray) ||
        PageDataOffset + RecordHeaderLength > PageSize ||
        (PageDataOffset & 7) != 0 ||
        LogSize > StreamSize ||
        (LogSize & (PageSize - 1)) != 0 ||
        LogSize <= FirstLogPage)
    {
        return STATUS_FILE_SYSTEM_LIMITATION;
    }
    OffsetBits = 64 - SeqNumberBits;

    ClusterSize = DiskVolume->BytesPerSector * (ULONG)DiskVolume->SectorsPerCluster;
    FileRecordSize = DiskVolume->MFT->FileRecordSize;
    IndexBufferSize = DiskVolume->ClustersPerIndexRecord < 0
        ? 1UL << -DiskVolume->ClustersPerIndexRecord
        : (ULONG)DiskVolume->ClustersPerIndexRecord * ClusterSize;
    if (!ClusterSize || !FileRecordSize || !IndexBufferSize)
        return STATUS_FILE_SYSTEM_LIMITATION;

    BootData = new(PagedPool, TAG_LOG_FILE_SERVICE) UCHAR[DiskVolume->BytesPerSector];
    if (!BootData)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = DiskVolume->ReadVolume(0, DiskVolume->BytesPerSector, BootData);
    if (NT_SUCCESS(Status))
    {
        Boot = reinterpret_cast<BootSector*>(BootData);
        MftLcn = Boot->MFTLCN;
        MftMirrorLcn = Boot->MFTMirrLCN;
    }
    delete[] BootData;
    if (!NT_SUCCESS(Status))
        return Status;

    Buffers = new(PagedPool, TAG_LOG_FILE_SERVICE) UCHAR[LfsBufferPageCount * PageSize];
    HomePages = new(PagedPool, TAG_LOG_FILE_SERVICE) UCHAR[2 * PageSize];
    if (!Buffers || !HomePages)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (ULONG Index = 0; Index < LfsBufferPageCount; Index++)
    {
        PUCHAR Page = Buffers + Index * PageSize;
        PLfsRecordPage Header = reinterpret_cast<PLfsRecordPage>(Page);
        ULONGLONG Offset;
        ULONG InPage;

        BufferHome[Index] = NoLsn;
        BufferValid[Index] = ReadLogPage((ULONGLONG)(LfsFirstBufferPage + Index) * PageSize, Page);
        if (!BufferValid[Index])
            continue;

        Offset = LsnToOffset(Header->LastLsnOrFileOffset);
        InPage = (ULONG)(Offset & (PageSize - 1));
        if (InPage >= PageDataOffset &&
            InPage + RecordHeaderLength <= PageSize &&
            reinterpret_cast<PLfsRecord>(Page + InPage)->ThisLsn == Header->LastLsnOrFileOffset)
        {
            BufferHome[Index] = Offset - InPage;
        }
    }

    return STATUS_SUCCESS;
}

PUCHAR
LogRecovery::HomePage(_In_ ULONGLONG Home)
{
    ULONG Slot;

    for (Slot = 0; Slot < 2; Slot++)
    {
        if (HomeOffset[Slot] == Home)
            return HomeValid[Slot] ? HomePages + Slot * PageSize : NULL;
    }

    Slot = NextHomeSlot;
    NextHomeSlot ^= 1;
    HomeOffset[Slot] = Home;
    HomeValid[Slot] = ReadLogPage(Home, HomePages + Slot * PageSize);
    return HomeValid[Slot] ? HomePages + Slot * PageSize : NULL;
}

PUCHAR
LogRecovery::FindStartPage(_In_ ULONGLONG Lsn,
                           _In_ ULONGLONG Home,
                           _In_ ULONG InPage,
                           _Out_ LONG* Source)
{
    PUCHAR Best = NULL;
    PUCHAR Page;
    ULONGLONG BestLsn = 0;

    *Source = SourceHome;
    if (Home < FirstLogPage || Home >= LogSize ||
        InPage < PageDataOffset || InPage + RecordHeaderLength > PageSize)
    {
        return NULL;
    }

    Page = HomePage(Home);
    if (Page && reinterpret_cast<PLfsRecord>(Page + InPage)->ThisLsn == Lsn)
    {
        Best = Page;
        BestLsn = reinterpret_cast<PLfsRecordPage>(Page)->LastLsnOrFileOffset;
    }

    for (ULONG Index = 0; Index < LfsBufferPageCount; Index++)
    {
        if (!BufferValid[Index] || BufferHome[Index] != Home)
            continue;

        Page = Buffers + Index * PageSize;
        if (reinterpret_cast<PLfsRecord>(Page + InPage)->ThisLsn != Lsn)
            continue;
        if (!Best || reinterpret_cast<PLfsRecordPage>(Page)->LastLsnOrFileOffset > BestLsn)
        {
            Best = Page;
            BestLsn = reinterpret_cast<PLfsRecordPage>(Page)->LastLsnOrFileOffset;
            *Source = (LONG)Index;
        }
    }

    return Best;
}

BOOLEAN
LogRecovery::TailMatches(_In_ PUCHAR Page,
                         _In_ ULONGLONG Home,
                         _In_ ULONGLONG Lsn,
                         _In_ ULONG End) const
{
    PLfsRecordPage Header = reinterpret_cast<PLfsRecordPage>(Page);
    ULONGLONG Next;

    if (!(Header->Flags & LfsPageRecordEnd) ||
        Header->LastEndLsn < Lsn ||
        Header->LastLsnOrFileOffset < Lsn)
    {
        return FALSE;
    }
    if (Header->LastEndLsn == Lsn)
        return Header->NextRecordOffset == End;
    if (End + RecordHeaderLength > PageSize)
        return FALSE;

    Next = reinterpret_cast<PLfsRecord>(Page + End)->ThisLsn;
    return LsnToOffset(Next) == Home + End && Next > Lsn;
}

PUCHAR
LogRecovery::FindContinuationPage(_In_ ULONGLONG Home,
                                  _In_ ULONGLONG Lsn,
                                  _In_ BOOLEAN Ends,
                                  _In_ ULONG End,
                                  _In_ LONG StartSource,
                                  _In_ ULONG Index)
{
    PUCHAR Best = NULL;
    ULONGLONG BestLsn = 0;

    for (LONG Source = SourceHome; Source < (LONG)LfsBufferPageCount; Source++)
    {
        PLfsRecordPage Header;
        PUCHAR Page;
        BOOLEAN Placed;

        if (Source == SourceHome)
        {
            Page = HomePage(Home);
            if (!Page)
                continue;
            Placed = TRUE;
        }
        else
        {
            if (!BufferValid[Source])
                continue;
            Page = Buffers + Source * PageSize;
            Placed = BufferHome[Source] == Home;
            if (!Placed &&
                (BufferHome[Source] != NoLsn ||
                 reinterpret_cast<PLfsRecordPage>(Page)->LastLsnOrFileOffset != Lsn))
            {
                continue;
            }
        }

        Header = reinterpret_cast<PLfsRecordPage>(Page);
        if (Ends)
        {
            if (!TailMatches(Page, Home, Lsn, End))
                continue;
        }
        else
        {
            if (Header->LastLsnOrFileOffset != Lsn ||
                ((Header->Flags & LfsPageRecordEnd) && Header->LastEndLsn == Lsn))
            {
                continue;
            }
            if (!Placed &&
                !(StartSource >= 0 &&
                  Source == StartSource + (LONG)Index &&
                  (ULONG)Source / LfsBufferHalf == (ULONG)StartSource / LfsBufferHalf))
            {
                continue;
            }
        }

        if (!Best || Header->LastLsnOrFileOffset > BestLsn)
        {
            Best = Page;
            BestLsn = Header->LastLsnOrFileOffset;
        }
    }

    return Best;
}

NTSTATUS
LogRecovery::ReadRecord(_In_ ULONGLONG Lsn,
                        _Out_ PULONGLONG NextLsn)
{
    ULONGLONG Offset = LsnToOffset(Lsn);
    ULONG InPage = (ULONG)(Offset & (PageSize - 1));
    ULONGLONG Home = Offset - InPage;
    ULONGLONG Sequence = Lsn >> OffsetBits;
    ULONGLONG NextOffset;
    LONG StartSource;
    PUCHAR Page;
    ULONG DataLength, Total, Copied = 0, Index = 0;

    Page = FindStartPage(Lsn, Home, InPage, &StartSource);
    if (!Page)
        return STATUS_NOT_FOUND;

    DataLength = reinterpret_cast<PLfsRecord>(Page + InPage)->ClientDataLength;
    if (DataLength > LogSize / 4)
        return STATUS_NOT_FOUND;

    Total = RecordHeaderLength + DataLength;
    if (Total > RecordCapacity)
    {
        PUCHAR NewRecord = new(PagedPool, TAG_LOG_FILE_SERVICE) UCHAR[Total + PageSize];

        if (!NewRecord)
            return STATUS_INSUFFICIENT_RESOURCES;
        delete[] Record;
        Record = NewRecord;
        RecordCapacity = Total + PageSize;
    }

    for (;;)
    {
        ULONG Take = min(Total - Copied, PageSize - InPage);
        ULONG Left;
        BOOLEAN Ends;

        RtlCopyMemory(Record + Copied, Page + InPage, Take);
        Copied += Take;
        InPage += Take;
        if (Copied == Total)
            break;

        Home = NextHome(Home);
        Index++;
        Left = Total - Copied;
        Ends = Left <= PageSize - PageDataOffset;
        Page = FindContinuationPage(Home,
                                    Lsn,
                                    Ends,
                                    Ends ? ALIGN_UP_BY(PageDataOffset + Left, 8) : 0,
                                    StartSource,
                                    Index);
        if (!Page)
            return STATUS_NOT_FOUND;
        InPage = PageDataOffset;
    }

    InPage = ALIGN_UP_BY(InPage, 8);
    if (InPage + RecordHeaderLength > PageSize)
    {
        Home = NextHome(Home);
        InPage = PageDataOffset;
    }

    NextOffset = Home + InPage;
    if (NextOffset <= Offset)
        Sequence++;
    *NextLsn = (Sequence << OffsetBits) | (NextOffset >> 3);
    RecordLength = Total;
    return STATUS_SUCCESS;
}

NTSTATUS
LogRecovery::ReadClientRecord(_In_ ULONGLONG Lsn,
                              _Out_ PNtfsLogRecord* Client,
                              _Out_ PULONG ClientLength,
                              _Out_ PULONGLONG NextLsn)
{
    PNtfsLogRecord Log;
    ULONG Length;
    NTSTATUS Status;

    *Client = NULL;
    *ClientLength = 0;
    Status = ReadRecord(Lsn, NextLsn);
    if (!NT_SUCCESS(Status))
        return Status;
    if (RecordHeader()->RecordType != LfsClientRecord)
        return STATUS_SUCCESS;

    Length = RecordHeader()->ClientDataLength;
    Log = reinterpret_cast<PNtfsLogRecord>(Record + RecordHeaderLength);
    if (Length < FIELD_OFFSET(NtfsLogRecord, LCNsForPage) ||
        Length - FIELD_OFFSET(NtfsLogRecord, LCNsForPage) < (ULONG)Log->LCNsToFollow * sizeof(ULONGLONG))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    *Client = Log;
    *ClientLength = Length;
    return STATUS_SUCCESS;
}

static NTSTATUS
RecordData(_In_ PNtfsLogRecord Log,
           _In_ ULONG ClientLength,
           _In_ ULONG Offset,
           _In_ ULONG Length,
           _Out_ const UCHAR** Data)
{
    *Data = NULL;
    if (Length == 0 || Offset >= ClientLength)
        return STATUS_SUCCESS;
    if (Length > ClientLength - Offset)
        return STATUS_FILE_CORRUPT_ERROR;

    *Data = reinterpret_cast<const UCHAR*>(Log) + Offset;
    return STATUS_SUCCESS;
}

NTSTATUS
LogRecovery::ReadTable(_In_ ULONGLONG Lsn,
                       _In_ USHORT Operation,
                       _Out_ PRestartTable* Table,
                       _Out_ PULONG TableLength)
{
    PNtfsLogRecord Log;
    const UCHAR* Data;
    ULONGLONG NextLsn;
    ULONG ClientLength;
    NTSTATUS Status;

    *Table = NULL;
    *TableLength = 0;
    Status = ReadClientRecord(Lsn, &Log, &ClientLength, &NextLsn);
    if (Status == STATUS_NOT_FOUND)
        return STATUS_FILE_CORRUPT_ERROR;
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Log || Log->RedoOperation != Operation || Log->RedoLength < sizeof(RestartTable))
        return STATUS_FILE_CORRUPT_ERROR;

    Status = RecordData(Log, ClientLength, Log->RedoOffset, Log->RedoLength, &Data);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Data)
        return STATUS_FILE_CORRUPT_ERROR;

    *Table = reinterpret_cast<PRestartTable>(const_cast<PUCHAR>(Data));
    *TableLength = Log->RedoLength;
    return STATUS_SUCCESS;
}

OpenAttributeInfo*
LogRecovery::OpenAttribute(_In_ ULONG Index,
                           _In_ BOOLEAN Create)
{
    ULONG Slot;

    if (Index < sizeof(RestartTable) ||
        (Index - sizeof(RestartTable)) % sizeof(OpenAttributeEntry3) != 0)
    {
        return NULL;
    }

    Slot = (Index - sizeof(RestartTable)) / sizeof(OpenAttributeEntry3);
    if (Slot >= OpenAttributeCount)
    {
        ULONG NewCount = Slot + 64;
        OpenAttributeInfo* NewTable;

        if (!Create)
            return NULL;
        NewTable = new(PagedPool, TAG_LOG_FILE_SERVICE) OpenAttributeInfo[NewCount];
        if (!NewTable)
            return NULL;
        RtlZeroMemory(NewTable, NewCount * sizeof(OpenAttributeInfo));
        if (OpenAttributes)
            RtlCopyMemory(NewTable, OpenAttributes, OpenAttributeCount * sizeof(OpenAttributeInfo));
        delete[] OpenAttributes;
        OpenAttributes = NewTable;
        OpenAttributeCount = NewCount;
    }

    return &OpenAttributes[Slot];
}

TransactionInfo*
LogRecovery::Transaction(_In_ ULONG Index,
                         _In_ BOOLEAN Create)
{
    ULONG Slot;

    if (Index < sizeof(RestartTable) ||
        (Index - sizeof(RestartTable)) % sizeof(TransactionEntry) != 0)
    {
        return NULL;
    }

    Slot = (Index - sizeof(RestartTable)) / sizeof(TransactionEntry);
    if (Slot >= TransactionCount)
    {
        ULONG NewCount = Slot + 16;
        TransactionInfo* NewTable;

        if (!Create)
            return NULL;
        NewTable = new(PagedPool, TAG_LOG_FILE_SERVICE) TransactionInfo[NewCount];
        if (!NewTable)
            return NULL;
        RtlZeroMemory(NewTable, NewCount * sizeof(TransactionInfo));
        if (Transactions)
            RtlCopyMemory(NewTable, Transactions, TransactionCount * sizeof(TransactionInfo));
        delete[] Transactions;
        Transactions = NewTable;
        TransactionCount = NewCount;
    }

    return &Transactions[Slot];
}

NTSTATUS
LogRecovery::LoadRestartTables(_In_ PRestartArea Area)
{
    ULONGLONG OpenAttributeLsn = Area->OpenAttributeTableLsn;
    ULONGLONG DirtyPageLsn = Area->DirtyPageTableLsn;
    ULONGLONG TransactionLsn = Area->TransactionTableLsn;
    PRestartTable Table;
    ULONG TableLength;
    NTSTATUS Status;

    if (OpenAttributeLsn)
    {
        Status = ReadTable(OpenAttributeLsn, OpenAttributeTableDump, &Table, &TableLength);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Table->EntrySize != sizeof(OpenAttributeEntry3))
            return STATUS_FILE_SYSTEM_LIMITATION;

        for (ULONG Index = 0; Index < Table->NumberEntries; Index++)
        {
            ULONG Offset = sizeof(RestartTable) + Index * Table->EntrySize;
            POpenAttributeEntry3 Entry;
            OpenAttributeInfo* Info;

            if (Offset + Table->EntrySize > TableLength)
                break;
            Entry = reinterpret_cast<POpenAttributeEntry3>(reinterpret_cast<PUCHAR>(Table) + Offset);
            if (Entry->AllocatedOrNextFree != RestartEntryAllocated)
                continue;

            Info = OpenAttribute(Offset, TRUE);
            if (!Info)
                return STATUS_INSUFFICIENT_RESOURCES;
            Info->Valid = TRUE;
            Info->Type = Entry->AttributeTypeCode;
            Info->BytesPerIndexBuffer = Entry->BytesPerIndexBuffer;
            Info->FileReference = Entry->FileReference;
        }
    }

    if (DirtyPageLsn)
    {
        Status = ReadTable(DirtyPageLsn, DirtyPageTableDump, &Table, &TableLength);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Table->EntrySize < FIELD_OFFSET(DirtyPageEntry2, LCNsForPage))
            return STATUS_FILE_SYSTEM_LIMITATION;

        for (ULONG Index = 0; Index < Table->NumberEntries; Index++)
        {
            ULONG Offset = sizeof(RestartTable) + Index * Table->EntrySize;
            PDirtyPageEntry2 Entry;
            PULONGLONG Lcns;

            if (Offset + Table->EntrySize > TableLength)
                break;
            Entry = reinterpret_cast<PDirtyPageEntry2>(reinterpret_cast<PUCHAR>(Table) + Offset);
            if (Entry->AllocatedOrNextFree != RestartEntryAllocated)
                continue;
            if (Entry->LCNsToFollow >
                (Table->EntrySize - FIELD_OFFSET(DirtyPageEntry2, LCNsForPage)) / sizeof(ULONGLONG))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            Lcns = &Entry->LCNsForPage;
            for (ULONG Page = 0; Page < Entry->LCNsToFollow; Page++)
            {
                LcnSlot* Slot = Clusters.Insert(Lcns[Page]);

                if (!Slot)
                    return STATUS_INSUFFICIENT_RESOURCES;
                if (Entry->OldestLsn < Slot->OldestLsn)
                    Slot->OldestLsn = Entry->OldestLsn;
            }
        }
    }

    if (TransactionLsn)
    {
        Status = ReadTable(TransactionLsn, TransactionTableDump, &Table, &TableLength);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Table->EntrySize != sizeof(TransactionEntry))
            return STATUS_FILE_SYSTEM_LIMITATION;

        for (ULONG Index = 0; Index < Table->NumberEntries; Index++)
        {
            ULONG Offset = sizeof(RestartTable) + Index * Table->EntrySize;
            PTransactionEntry Entry;
            TransactionInfo* Info;

            if (Offset + Table->EntrySize > TableLength)
                break;
            Entry = reinterpret_cast<PTransactionEntry>(reinterpret_cast<PUCHAR>(Table) + Offset);
            if (Entry->AllocatedOrNextFree != RestartEntryAllocated)
                continue;

            Info = Transaction(Offset, TRUE);
            if (!Info)
                return STATUS_INSUFFICIENT_RESOURCES;
            Info->Active = TRUE;
            Info->UndoNextLsn = Entry->UndoNextLsn;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
LogRecovery::Analyze()
{
    PRestartArea Area;
    ULONGLONG Lsn, NextLsn, Newest;
    NTSTATUS Status;

    Status = ReadRecord(ClientRestartLsn, &NextLsn);
    if (!NT_SUCCESS(Status))
        return Status == STATUS_NOT_FOUND ? STATUS_FILE_CORRUPT_ERROR : Status;
    if (RecordHeader()->RecordType != LfsClientRestart ||
        RecordHeader()->ClientDataLength < sizeof(RestartArea))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Area = reinterpret_cast<PRestartArea>(Record + RecordHeaderLength);
    if (Area->MajorVersion != NtfsRestartMajorVersion)
        return STATUS_FILE_SYSTEM_LIMITATION;
    Lsn = Area->StartOfCheckpointLsn ? Area->StartOfCheckpointLsn : ClientRestartLsn;

    Status = LoadRestartTables(Area);
    if (!NT_SUCCESS(Status))
        return Status;

    for (;;)
    {
        PNtfsLogRecord Log;
        ULONG ClientLength;

        Status = ReadClientRecord(Lsn, &Log, &ClientLength, &NextLsn);
        if (Status == STATUS_NOT_FOUND)
            break;
        if (!NT_SUCCESS(Status))
            return Status;

        LastLsn = Lsn;
        Lsn = NextLsn;
        if (!Log)
            continue;

        switch (Log->RedoOperation)
        {
            case OpenAttributeTableDump:
            case AttributeNamesDump:
            case DirtyPageTableDump:
            case TransactionTableDump:
                continue;
            case HotFix:
            case PrepareTransaction:
                return STATUS_FILE_SYSTEM_LIMITATION;
            default:
                break;
        }
        if (Log->RedoOperation > ZeroEndOfFileRecord || Log->UndoOperation > ZeroEndOfFileRecord)
            return STATUS_FILE_SYSTEM_LIMITATION;

        if (Log->RedoOperation == CommitTransaction || Log->RedoOperation == ForgetTransaction)
        {
            TransactionInfo* Info = Transaction(RecordHeader()->TransactionId, FALSE);

            if (Info)
                Info->Active = FALSE;
        }
        else if (RecordHeader()->TransactionId >= sizeof(RestartTable) &&
                 (RecordHeader()->TransactionId - sizeof(RestartTable)) % sizeof(TransactionEntry) == 0)
        {
            TransactionInfo* Info = Transaction(RecordHeader()->TransactionId, TRUE);

            if (!Info)
                return STATUS_INSUFFICIENT_RESOURCES;
            Info->Active = TRUE;
            Info->UndoNextLsn = Log->UndoOperation == CompensationLogRecord
                ? RecordHeader()->ClientUndoNextLsn
                : LastLsn;
        }

        if (Log->RedoOperation == OpenNonResidentAttribute)
        {
            const UCHAR* Data;
            POpenAttributeEntry3 Entry;
            OpenAttributeInfo* Info;

            Status = RecordData(Log, ClientLength, Log->RedoOffset, Log->RedoLength, &Data);
            if (!NT_SUCCESS(Status))
                return Status;
            if (!Data || Log->RedoLength < sizeof(OpenAttributeEntry3))
                return STATUS_FILE_SYSTEM_LIMITATION;

            Info = OpenAttribute(Log->TargetAttributeOffset, TRUE);
            if (!Info)
                return STATUS_FILE_CORRUPT_ERROR;
            Entry = reinterpret_cast<POpenAttributeEntry3>(const_cast<PUCHAR>(Data));
            Info->Valid = TRUE;
            Info->Type = Entry->AttributeTypeCode;
            Info->BytesPerIndexBuffer = Entry->BytesPerIndexBuffer;
            Info->FileReference = Entry->FileReference;
        }
        else if (Log->RedoOperation == DeleteDirtyClusters)
        {
            const UCHAR* Data;
            const ULONGLONG* Ranges;

            Status = RecordData(Log, ClientLength, Log->RedoOffset, Log->RedoLength, &Data);
            if (!NT_SUCCESS(Status))
                return Status;
            Ranges = reinterpret_cast<const ULONGLONG*>(Data);
            for (ULONG Range = 0; Data && Range < Log->RedoLength / (2 * sizeof(ULONGLONG)); Range++)
            {
                ULONGLONG First = Ranges[2 * Range];
                ULONGLONG Count = Ranges[2 * Range + 1];

                for (ULONG Index = 0; Index < Clusters.Capacity; Index++)
                {
                    LcnSlot* Slot = &Clusters.Slots[Index];

                    if (Slot->Used && Slot->Lcn >= First && Slot->Lcn - First < Count)
                        Slot->OldestLsn = NoLsn;
                }
            }
        }

        for (ULONG Index = 0; Index < Log->LCNsToFollow; Index++)
        {
            ULONGLONG Lcn = reinterpret_cast<PULONGLONG>(&Log->LCNsForPage)[Index];
            LcnSlot* Slot;

            if (Lcn >= DiskVolume->ClustersInVolume)
                return STATUS_FILE_CORRUPT_ERROR;
            Slot = Clusters.Insert(Lcn);
            if (!Slot)
                return STATUS_INSUFFICIENT_RESOURCES;
            if (Slot->OldestLsn == NoLsn)
                Slot->OldestLsn = LastLsn;
        }
    }

    Newest = CurrentLsn > ClientRestartLsn ? CurrentLsn : ClientRestartLsn;
    for (ULONG Index = 0; Index < LfsBufferPageCount; Index++)
    {
        PLfsRecordPage Header = reinterpret_cast<PLfsRecordPage>(Buffers + Index * PageSize);

        if (BufferValid[Index] && (Header->Flags & LfsPageRecordEnd) && Header->LastEndLsn > Newest)
            Newest = Header->LastEndLsn;
    }
    if (LastLsn < Newest)
    {
        DPRINT1("Log readable up to %08lx%08lx, but a page carries %08lx%08lx\n",
                (ULONG)(LastLsn >> 32), (ULONG)LastLsn, (ULONG)(Newest >> 32), (ULONG)Newest);
        return STATUS_FILE_SYSTEM_LIMITATION;
    }

    for (ULONG Index = 0; Index < Clusters.Capacity; Index++)
    {
        if (Clusters.Slots[Index].Used && Clusters.Slots[Index].OldestLsn < RedoStartLsn)
            RedoStartLsn = Clusters.Slots[Index].OldestLsn;
    }

    return STATUS_SUCCESS;
}

PUCHAR
LogRecovery::Cluster(_In_ ULONGLONG Lcn)
{
    LcnSlot* Slot;

    if (Lcn >= DiskVolume->ClustersInVolume)
        return NULL;
    Slot = Clusters.Insert(Lcn);
    if (!Slot)
        return NULL;
    if (Slot->Data)
        return Slot->Data;
    if (CachedBytes + ClusterSize > MaximumCachedBytes)
        return NULL;

    Slot->Data = new(PagedPool, TAG_LOG_FILE_SERVICE) UCHAR[ClusterSize];
    if (!Slot->Data)
        return NULL;
    if (!NT_SUCCESS(DiskVolume->ReadVolume(Lcn * ClusterSize, ClusterSize, Slot->Data)))
    {
        delete[] Slot->Data;
        Slot->Data = NULL;
        return NULL;
    }

    CachedBytes += ClusterSize;
    return Slot->Data;
}

NTSTATUS
LogRecovery::LoadBlock(_In_ const LogAction& Action,
                       _In_ ULONG Offset,
                       _In_ ULONG Size)
{
    if (Size == 0 || (ULONGLONG)Offset + Size > (ULONGLONG)Action.LcnCount * ClusterSize)
        return STATUS_FILE_CORRUPT_ERROR;

    if (Size > BlockCapacity)
    {
        delete[] Block;
        Block = new(PagedPool, TAG_LOG_FILE_SERVICE) UCHAR[Size];
        BlockCapacity = Block ? Size : 0;
        if (!Block)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (ULONG Done = 0; Done < Size;)
    {
        ULONG Position = Offset + Done;
        ULONG InCluster = Position % ClusterSize;
        ULONG Take = min(Size - Done, ClusterSize - InCluster);
        PUCHAR Data = Cluster(Action.Lcns[Position / ClusterSize]);

        if (!Data)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(Block + Done, Data + InCluster, Take);
        Done += Take;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
LogRecovery::StoreBlock(_In_ const LogAction& Action,
                        _In_ ULONG Offset,
                        _In_ ULONG Size)
{
    for (ULONG Done = 0; Done < Size;)
    {
        ULONG Position = Offset + Done;
        ULONG InCluster = Position % ClusterSize;
        ULONG Take = min(Size - Done, ClusterSize - InCluster);
        LcnSlot* Slot = Clusters.Find(Action.Lcns[Position / ClusterSize]);

        if (!Slot || !Slot->Data)
            return STATUS_INVALID_DEVICE_STATE;
        if (Action.Undo)
        {
            if (!Slot->RedoData)
            {
                Slot->RedoData = new(PagedPool, TAG_LOG_FILE_SERVICE) UCHAR[ClusterSize];
                if (!Slot->RedoData)
                    return STATUS_INSUFFICIENT_RESOURCES;
                RtlCopyMemory(Slot->RedoData, Slot->Data, ClusterSize);
            }
            Slot->UndoDirty = TRUE;
        }
        else
        {
            Slot->Dirty = TRUE;
        }
        RtlCopyMemory(Slot->Data + InCluster, Block + Done, Take);
        Done += Take;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
LogRecovery::LoadStructure(_In_ const LogAction& Action,
                           _In_ const char* Signature,
                           _In_ ULONG Size,
                           _Out_ PBOOLEAN Valid)
{
    NTSTATUS Status;

    *Valid = FALSE;
    Status = LoadBlock(Action, Action.ClusterBlockOffset * LogBlockSize, Size);
    if (!NT_SUCCESS(Status))
        return Status;

    *Valid = RtlCompareMemory(Block, Signature, 4) == 4 &&
             NT_SUCCESS(NtfsApplyFixup(reinterpret_cast<PNTFSRecordHeader>(Block),
                                       Size,
                                       DiskVolume->BytesPerSector));
    return STATUS_SUCCESS;
}

NTSTATUS
LogRecovery::StoreStructure(_In_ const LogAction& Action,
                            _In_ ULONG Size,
                            _In_ ULONGLONG Lsn)
{
    NTSTATUS Status;

    reinterpret_cast<PNTFSRecordHeader>(Block)->LogFileSequenceNumber = Lsn;
    Status = NtfsCommitFixup(reinterpret_cast<PNTFSRecordHeader>(Block),
                             Size,
                             DiskVolume->BytesPerSector);
    if (!NT_SUCCESS(Status))
        return Status;

    return StoreBlock(Action, Action.ClusterBlockOffset * LogBlockSize, Size);
}

NTSTATUS
LogRecovery::ResizeAttribute(_In_ ULONG AttributeStart,
                             _In_ ULONG NewLength)
{
    PFileRecordHeader Header = reinterpret_cast<PFileRecordHeader>(Block);
    PAttribute Attr = reinterpret_cast<PAttribute>(Block + AttributeStart);
    ULONG InUse = Header->ActualSize;
    ULONG OldLength = Attr->Length;

    if (OldLength > InUse - AttributeStart ||
        (ULONGLONG)InUse - OldLength + NewLength > FileRecordSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    RtlMoveMemory(Block + AttributeStart + NewLength,
                  Block + AttributeStart + OldLength,
                  InUse - AttributeStart - OldLength);
    Attr->Length = NewLength;
    Header->ActualSize = InUse - OldLength + NewLength;
    return STATUS_SUCCESS;
}

NTSTATUS
LogRecovery::ApplyToFileRecord(_In_ const LogAction& Action)
{
    PFileRecordHeader Header;
    PAttribute Attr;
    ULONG Start = Action.RecordOffset;
    ULONG Position = Start + Action.AttributeOffset;
    ULONG Length = Action.Length;
    ULONGLONG Lsn = Action.Lsn;
    ULONG InUse;
    BOOLEAN Valid;
    NTSTATUS Status;

    Status = LoadStructure(Action, "FILE", FileRecordSize, &Valid);
    if (!NT_SUCCESS(Status))
        return Status;

    Header = reinterpret_cast<PFileRecordHeader>(Block);
    if (Action.Undo)
    {
        if (!Valid || Header->Header.LogFileSequenceNumber < Action.Lsn)
            return STATUS_SUCCESS;
        Lsn = Header->Header.LogFileSequenceNumber;
    }
    else if (Valid && Header->Header.LogFileSequenceNumber >= Action.Lsn)
    {
        return STATUS_SUCCESS;
    }

    if (Action.Operation == InitializeFileRecordSegment)
    {
        if (!Action.Data || Start > FileRecordSize || Length > FileRecordSize - Start)
            return STATUS_FILE_CORRUPT_ERROR;
        if (!Valid)
            RtlZeroMemory(Block, FileRecordSize);
        RtlCopyMemory(Block + Start, Action.Data, Length);
        Count(Action);
        return StoreStructure(Action, FileRecordSize, Lsn);
    }

    if (!Valid)
        return STATUS_FILE_CORRUPT_ERROR;
    InUse = Header->ActualSize;
    if (InUse > FileRecordSize || (Start > InUse && Action.Operation != ZeroEndOfFileRecord))
        return STATUS_FILE_CORRUPT_ERROR;
    Attr = reinterpret_cast<PAttribute>(Block + Start);

    switch (Action.Operation)
    {
        case DeallocateFileRecordSegment:
            Header->Flags &= ~FR_IN_USE;
            if (++Header->SequenceNumber == 0)
                Header->SequenceNumber = 1;
            break;

        case ZeroEndOfFileRecord:
            if (Position > FileRecordSize)
                return STATUS_FILE_CORRUPT_ERROR;
            RtlZeroMemory(Block + Position, min(Length, FileRecordSize - Position));
            break;

        case CreateAttribute:
            if (!Action.Data || Length < FIELD_OFFSET(Attribute, Resident) ||
                Length > FileRecordSize - InUse)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            RtlMoveMemory(Block + Start + Length, Block + Start, InUse - Start);
            RtlCopyMemory(Block + Start, Action.Data, Length);
            Header->ActualSize = InUse + Length;
            if (Attr->AttributeID >= Header->NextAttributeID)
                Header->NextAttributeID = Attr->AttributeID + 1;
            if (Attr->AttributeType == TypeFileName)
                Header->HardLinkCount++;
            break;

        case DeleteAttribute:
            if (InUse - Start < FIELD_OFFSET(Attribute, Resident) || Attr->Length > InUse - Start)
                return STATUS_FILE_CORRUPT_ERROR;
            Length = Attr->Length;
            if (Attr->AttributeType == TypeFileName)
                Header->HardLinkCount--;
            RtlMoveMemory(Block + Start, Block + Start + Length, InUse - Start - Length);
            Header->ActualSize = InUse - Length;
            break;

        case UpdateResidentAttributeValue:
            if (Action.Length != Action.OtherLength)
            {
                if (InUse - Start < ResidentHeaderLength || Attr->IsNonResident ||
                    Action.AttributeOffset < Attr->Resident.DataOffset)
                {
                    return STATUS_FILE_CORRUPT_ERROR;
                }
                Status = ResizeAttribute(Start, ALIGN_UP_BY(Action.AttributeOffset + Length, 8));
                if (!NT_SUCCESS(Status))
                    return Status;
                Attr->Resident.DataLength = Action.AttributeOffset + Length - Attr->Resident.DataOffset;
            }
            if (Position > Header->ActualSize || Length > Header->ActualSize - Position)
                return STATUS_FILE_CORRUPT_ERROR;
            if (Action.Data)
                RtlCopyMemory(Block + Position, Action.Data, Length);
            else
                RtlZeroMemory(Block + Position, Length);
            break;

        case UpdateMappingPairs:
        {
            ULONG NewLength = ALIGN_UP_BY(Action.AttributeOffset + Length, 8);
            ULONGLONG RunClusters = 0;
            ULONG Run;

            if (InUse - Start < NonResidentHeaderLength || !Attr->IsNonResident || !Action.Data ||
                Action.AttributeOffset < Attr->NonResident.DataRunsOffset)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            if (NewLength != Attr->Length)
            {
                Status = ResizeAttribute(Start, NewLength);
                if (!NT_SUCCESS(Status))
                    return Status;
            }
            RtlCopyMemory(Block + Position, Action.Data, Length);
            RtlZeroMemory(Block + Position + Length, NewLength - Action.AttributeOffset - Length);

            for (Run = Start + Attr->NonResident.DataRunsOffset; Run < Start + NewLength && Block[Run];)
            {
                ULONG LengthBytes = Block[Run] & 0x0f;
                ULONG OffsetBytes = Block[Run] >> 4;
                ULONGLONG RunLength = 0;

                if (LengthBytes > 8 || OffsetBytes > 8 ||
                    Run + 1 + LengthBytes + OffsetBytes > Start + NewLength)
                {
                    return STATUS_FILE_CORRUPT_ERROR;
                }
                RtlCopyMemory(&RunLength, Block + Run + 1, LengthBytes);
                RunClusters += RunLength;
                Run += 1 + LengthBytes + OffsetBytes;
            }
            Attr->NonResident.LastVCN = Attr->NonResident.FirstVCN + RunClusters - 1;
            break;
        }

        case SetNewAttributeSizes:
            if (InUse - Start < NonResidentHeaderLength ||
                !Attr->IsNonResident || !Action.Data || Length < 3 * sizeof(ULONGLONG))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            RtlCopyMemory(&Attr->NonResident.AllocatedSize, Action.Data, sizeof(ULONGLONG));
            RtlCopyMemory(&Attr->NonResident.InitalizedDataSize, Action.Data + 8, sizeof(ULONGLONG));
            RtlCopyMemory(&Attr->NonResident.DataSize, Action.Data + 16, sizeof(ULONGLONG));
            if (Length >= 4 * sizeof(ULONGLONG))
            {
                if (Attr->Length < sizeof(Attribute))
                    return STATUS_FILE_CORRUPT_ERROR;
                RtlCopyMemory(&Attr->NonResident.CompressedDataSize, Action.Data + 24, sizeof(ULONGLONG));
            }
            break;

        case AddIndexEntryToRoot:
        case DeleteIndexEntryFromRoot:
        {
            PIndexNodeHeader Node;
            ULONG OldLength, NodeOffset;
            LONG Delta;

            if (InUse - Start < ResidentHeaderLength || Attr->IsNonResident ||
                Attr->Length > InUse - Start ||
                Action.AttributeOffset > Attr->Length)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            OldLength = Attr->Length;
            NodeOffset = Start + Attr->Resident.DataOffset + FIELD_OFFSET(IndexRootEx, Header);
            if (NodeOffset + sizeof(IndexNodeHeader) > Start + OldLength)
                return STATUS_FILE_CORRUPT_ERROR;

            if (Action.Operation == AddIndexEntryToRoot)
            {
                if (!Action.Data || Length < FIELD_OFFSET(IndexEntry, IndexStream))
                    return STATUS_FILE_CORRUPT_ERROR;
                Delta = (LONG)Length;
                Status = ResizeAttribute(Start, OldLength + Length);
                if (!NT_SUCCESS(Status))
                    return Status;
                RtlMoveMemory(Block + Position + Length,
                              Block + Position,
                              Start + OldLength - Position);
                RtlCopyMemory(Block + Position, Action.Data, Length);
            }
            else
            {
                if (Start + OldLength - Position < FIELD_OFFSET(IndexEntry, IndexStream))
                    return STATUS_FILE_CORRUPT_ERROR;
                Length = reinterpret_cast<PIndexEntry>(Block + Position)->EntryLength;
                if (Length < FIELD_OFFSET(IndexEntry, IndexStream) ||
                    Length > Start + OldLength - Position)
                {
                    return STATUS_FILE_CORRUPT_ERROR;
                }
                Delta = -(LONG)Length;
                RtlMoveMemory(Block + Position,
                              Block + Position + Length,
                              Start + OldLength - Position - Length);
                Status = ResizeAttribute(Start, OldLength - Length);
                if (!NT_SUCCESS(Status))
                    return Status;
            }

            Node = reinterpret_cast<PIndexNodeHeader>(Block + NodeOffset);
            Attr->Resident.DataLength += Delta;
            Node->TotalIndexSize += Delta;
            Node->AllocatedSize += Delta;
            break;
        }

        case SetIndexEntryVcnInRoot:
        {
            PIndexEntry Entry = reinterpret_cast<PIndexEntry>(Block + Position);

            if (!Action.Data || Length != sizeof(ULONGLONG) ||
                Position + FIELD_OFFSET(IndexEntry, IndexStream) > InUse ||
                Entry->EntryLength < FIELD_OFFSET(IndexEntry, IndexStream) + sizeof(ULONGLONG) ||
                Position + Entry->EntryLength > InUse)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            RtlCopyMemory(Block + Position + Entry->EntryLength - sizeof(ULONGLONG), Action.Data, Length);
            break;
        }

        case UpdateFileNameInRoot:
            Position += FIELD_OFFSET(IndexEntry, IndexStream) + sizeof(ULONGLONG);
            if (!Action.Data || Position > InUse || Length > InUse - Position)
                return STATUS_FILE_CORRUPT_ERROR;
            RtlCopyMemory(Block + Position, Action.Data, Length);
            break;

        default:
            return STATUS_FILE_SYSTEM_LIMITATION;
    }

    Count(Action);
    return StoreStructure(Action, FileRecordSize, Lsn);
}

NTSTATUS
LogRecovery::ApplyToIndexBuffer(_In_ const LogAction& Action)
{
    OpenAttributeInfo* Info = OpenAttribute(Action.TargetAttribute, FALSE);
    ULONG Size = Info && Info->Valid && Info->BytesPerIndexBuffer ? Info->BytesPerIndexBuffer : IndexBufferSize;
    ULONG Position = (ULONG)Action.RecordOffset + Action.AttributeOffset;
    ULONG Length = Action.Length;
    ULONGLONG Lsn = Action.Lsn;
    PIndexBuffer Buffer;
    PIndexEntry Entry;
    ULONG Used;
    BOOLEAN Valid;
    NTSTATUS Status;

    if (Size < sizeof(IndexBuffer) || (Size & (DiskVolume->BytesPerSector - 1)) != 0)
        return STATUS_FILE_CORRUPT_ERROR;

    Status = LoadStructure(Action, "INDX", Size, &Valid);
    if (!NT_SUCCESS(Status))
        return Status;

    Buffer = reinterpret_cast<PIndexBuffer>(Block);
    if (Action.Undo)
    {
        if (!Valid || Buffer->RecordHeader.LogFileSequenceNumber < Action.Lsn)
            return STATUS_SUCCESS;
        Lsn = Buffer->RecordHeader.LogFileSequenceNumber;
    }
    else if (Valid && Buffer->RecordHeader.LogFileSequenceNumber >= Action.Lsn)
    {
        return STATUS_SUCCESS;
    }

    if (Action.Operation == UpdateNonResidentAttributeValue)
    {
        if (Position > Size || Length > Size - Position)
            return STATUS_FILE_CORRUPT_ERROR;
        if (!Valid)
            RtlZeroMemory(Block, Size);
        if (Action.Data)
            RtlCopyMemory(Block + Position, Action.Data, Length);
        else
            RtlZeroMemory(Block + Position, Length);
        Count(Action);
        return StoreStructure(Action, Size, Lsn);
    }

    if (!Valid)
        return STATUS_FILE_CORRUPT_ERROR;
    Used = FIELD_OFFSET(IndexBuffer, IndexHeader) + Buffer->IndexHeader.TotalIndexSize;
    if (Used > Size || Position > Size)
        return STATUS_FILE_CORRUPT_ERROR;
    Entry = reinterpret_cast<PIndexEntry>(Block + Position);

    switch (Action.Operation)
    {
        case AddIndexEntryToAllocationBuffer:
            if (!Action.Data || Position > Used || Length > Size - Used ||
                Length < FIELD_OFFSET(IndexEntry, IndexStream))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            RtlMoveMemory(Block + Position + Length, Block + Position, Used - Position);
            RtlCopyMemory(Block + Position, Action.Data, Length);
            Buffer->IndexHeader.TotalIndexSize += Length;
            break;

        case DeleteIndexEntryFromAllocationBuffer:
            if (Position + FIELD_OFFSET(IndexEntry, IndexStream) > Used)
                return STATUS_FILE_CORRUPT_ERROR;
            Length = Entry->EntryLength;
            if (Length < FIELD_OFFSET(IndexEntry, IndexStream) || Length > Used - Position)
                return STATUS_FILE_CORRUPT_ERROR;
            RtlMoveMemory(Block + Position, Block + Position + Length, Used - Position - Length);
            Buffer->IndexHeader.TotalIndexSize -= Length;
            break;

        case WriteEndOfIndexBuffer:
            if (!Action.Data || Length > Size - Position ||
                Position + Length < FIELD_OFFSET(IndexBuffer, IndexHeader))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            RtlCopyMemory(Block + Position, Action.Data, Length);
            Buffer->IndexHeader.TotalIndexSize = Position + Length - FIELD_OFFSET(IndexBuffer, IndexHeader);
            break;

        case SetIndexEntryVcnInAllocationBuffer:
            if (!Action.Data || Length != sizeof(ULONGLONG) ||
                Position + FIELD_OFFSET(IndexEntry, IndexStream) > Used ||
                Entry->EntryLength < FIELD_OFFSET(IndexEntry, IndexStream) + sizeof(ULONGLONG) ||
                Entry->EntryLength > Used - Position)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            RtlCopyMemory(Block + Position + Entry->EntryLength - sizeof(ULONGLONG), Action.Data, Length);
            break;

        case UpdateFileNameInAllocationBuffer:
            Position += FIELD_OFFSET(IndexEntry, IndexStream) + sizeof(ULONGLONG);
            if (!Action.Data || Position > Used || Length > Used - Position)
                return STATUS_FILE_CORRUPT_ERROR;
            RtlCopyMemory(Block + Position, Action.Data, Length);
            break;

        default:
            return STATUS_FILE_SYSTEM_LIMITATION;
    }

    Count(Action);
    return StoreStructure(Action, Size, Lsn);
}

NTSTATUS
LogRecovery::ApplyToClusters(_In_ const LogAction& Action)
{
    ULONG Size = (ULONG)Action.LcnCount * ClusterSize;
    NTSTATUS Status;

    if (Action.Operation == UpdateNonResidentAttributeValue)
    {
        ULONG Position = Action.ClusterBlockOffset * LogBlockSize +
                         Action.RecordOffset + Action.AttributeOffset;

        if (Position > Size || Action.Length > Size - Position)
            return STATUS_FILE_CORRUPT_ERROR;
        Status = LoadBlock(Action, Position, Action.Length);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Action.Data)
            RtlCopyMemory(Block, Action.Data, Action.Length);
        else
            RtlZeroMemory(Block, Action.Length);
        Count(Action);
        return StoreBlock(Action, Position, Action.Length);
    }

    if (Action.Operation == SetBitsInNonResidentBitMap ||
        Action.Operation == ClearBitsInNonResidentBitMap)
    {
        const BitmapRange* Range = reinterpret_cast<const BitmapRange*>(Action.Data);

        if (!Action.Data || Action.Length < sizeof(BitmapRange) ||
            Action.ClusterBlockOffset || Action.RecordOffset || Action.AttributeOffset ||
            Range->BitmapOffset > Size * 8 || Range->NumberOfBits > Size * 8 - Range->BitmapOffset)
        {
            return STATUS_FILE_SYSTEM_LIMITATION;
        }
        Status = LoadBlock(Action, 0, Size);
        if (!NT_SUCCESS(Status))
            return Status;

        for (ULONG Bit = Range->BitmapOffset; Bit < Range->BitmapOffset + Range->NumberOfBits; Bit++)
        {
            if (Action.Operation == SetBitsInNonResidentBitMap)
                Block[Bit >> 3] |= (UCHAR)(1 << (Bit & 7));
            else
                Block[Bit >> 3] &= (UCHAR)~(1 << (Bit & 7));
        }
        Count(Action);
        return StoreBlock(Action, 0, Size);
    }

    return STATUS_FILE_SYSTEM_LIMITATION;
}

NTSTATUS
LogRecovery::Apply(_In_ const LogAction& Action)
{
    OpenAttributeInfo* Info;

    switch (Action.Operation)
    {
        case NoOp:
        case CompensationLogRecord:
        case DeleteDirtyClusters:
        case EndTopLevelAction:
        case CommitTransaction:
        case ForgetTransaction:
        case OpenNonResidentAttribute:
        case OpenAttributeTableDump:
        case AttributeNamesDump:
        case DirtyPageTableDump:
        case TransactionTableDump:
            return STATUS_SUCCESS;

        case InitializeFileRecordSegment:
        case DeallocateFileRecordSegment:
        case CreateAttribute:
        case DeleteAttribute:
        case UpdateResidentAttributeValue:
        case UpdateMappingPairs:
        case SetNewAttributeSizes:
        case AddIndexEntryToRoot:
        case DeleteIndexEntryFromRoot:
        case SetIndexEntryVcnInRoot:
        case UpdateFileNameInRoot:
        case ZeroEndOfFileRecord:
            return ApplyToFileRecord(Action);

        case AddIndexEntryToAllocationBuffer:
        case DeleteIndexEntryFromAllocationBuffer:
        case WriteEndOfIndexBuffer:
        case SetIndexEntryVcnInAllocationBuffer:
        case UpdateFileNameInAllocationBuffer:
            return ApplyToIndexBuffer(Action);

        case SetBitsInNonResidentBitMap:
        case ClearBitsInNonResidentBitMap:
            return ApplyToClusters(Action);

        case UpdateNonResidentAttributeValue:
            Info = OpenAttribute(Action.TargetAttribute, FALSE);
            if (!Info || !Info->Valid)
                return STATUS_FILE_SYSTEM_LIMITATION;
            if (Info->Type == TypeIndexAllocation)
                return ApplyToIndexBuffer(Action);
            if (Info->Type == TypeData && GetFRNFromFileRef(Info->FileReference) == _MFT)
                return STATUS_FILE_SYSTEM_LIMITATION;
            return ApplyToClusters(Action);

        default:
            return STATUS_FILE_SYSTEM_LIMITATION;
    }
}

NTSTATUS
LogRecovery::Redo()
{
    ULONGLONG Lsn = RedoStartLsn;

    if (Lsn == NoLsn)
        return STATUS_SUCCESS;

    for (;;)
    {
        PNtfsLogRecord Log;
        LogAction Action;
        ULONGLONG NextLsn;
        ULONG ClientLength;
        BOOLEAN Dirty = TRUE;
        NTSTATUS Status;

        Status = ReadClientRecord(Lsn, &Log, &ClientLength, &NextLsn);
        if (Status == STATUS_NOT_FOUND)
            return Lsn > LastLsn ? STATUS_SUCCESS : STATUS_FILE_CORRUPT_ERROR;
        if (!NT_SUCCESS(Status))
            return Status;

        Action.Lsn = Lsn;
        Lsn = NextLsn;
        if (!Log || Log->LCNsToFollow == 0)
            continue;

        Action.Lcns = reinterpret_cast<const ULONGLONG*>(&Log->LCNsForPage);
        Action.LcnCount = Log->LCNsToFollow;
        for (ULONG Index = 0; Index < Action.LcnCount; Index++)
        {
            LcnSlot* Slot = Clusters.Find(Action.Lcns[Index]);

            if (!Slot || Slot->OldestLsn > Action.Lsn)
                Dirty = FALSE;
        }
        if (!Dirty)
            continue;

        Action.Operation = Log->RedoOperation;
        Action.TargetAttribute = Log->TargetAttributeOffset;
        Action.RecordOffset = Log->RecordOffset;
        Action.AttributeOffset = Log->AttributeOffset;
        Action.ClusterBlockOffset = Log->ClusterBlockOffset;
        Action.Length = Log->RedoLength;
        Action.OtherLength = Log->UndoLength;
        Action.Undo = FALSE;
        Status = RecordData(Log, ClientLength, Log->RedoOffset, Log->RedoLength, &Action.Data);
        if (NT_SUCCESS(Status))
            Status = Apply(Action);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Redo of operation %x at %08lx%08lx failed (Status %lx)\n",
                    Action.Operation, (ULONG)(Action.Lsn >> 32), (ULONG)Action.Lsn, Status);
            return Status;
        }
    }
}

NTSTATUS
LogRecovery::Undo()
{
    for (;;)
    {
        TransactionInfo* Info = NULL;
        PNtfsLogRecord Log;
        LogAction Action;
        ULONGLONG NextLsn;
        ULONG ClientLength;
        NTSTATUS Status;

        for (ULONG Index = 0; Index < TransactionCount; Index++)
        {
            if (Transactions[Index].Active && Transactions[Index].UndoNextLsn != 0 &&
                (!Info || Transactions[Index].UndoNextLsn > Info->UndoNextLsn))
            {
                Info = &Transactions[Index];
            }
        }
        if (!Info)
            return STATUS_SUCCESS;

        Action.Lsn = Info->UndoNextLsn;
        Status = ReadClientRecord(Action.Lsn, &Log, &ClientLength, &NextLsn);
        if (Status == STATUS_NOT_FOUND)
            return STATUS_FILE_CORRUPT_ERROR;
        if (!NT_SUCCESS(Status))
            return Status;
        if (!Log || RecordHeader()->ClientUndoNextLsn >= Action.Lsn)
            return STATUS_FILE_CORRUPT_ERROR;

        if (Log->UndoOperation != NoOp && Log->UndoOperation != CompensationLogRecord)
        {
            Action.Lcns = reinterpret_cast<const ULONGLONG*>(&Log->LCNsForPage);
            Action.LcnCount = Log->LCNsToFollow;
            Action.Operation = Log->UndoOperation;
            Action.TargetAttribute = Log->TargetAttributeOffset;
            Action.RecordOffset = Log->RecordOffset;
            Action.AttributeOffset = Log->AttributeOffset;
            Action.ClusterBlockOffset = Log->ClusterBlockOffset;
            Action.Length = Log->UndoLength;
            Action.OtherLength = Log->RedoLength;
            Action.Undo = TRUE;
            Status = RecordData(Log, ClientLength, Log->UndoOffset, Log->UndoLength, &Action.Data);
            if (NT_SUCCESS(Status))
                Status = Apply(Action);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Undo of operation %x at %08lx%08lx failed (Status %lx)\n",
                        Action.Operation, (ULONG)(Action.Lsn >> 32), (ULONG)Action.Lsn, Status);
                return Status;
            }
        }

        Info->UndoNextLsn = RecordHeader()->ClientUndoNextLsn;
    }
}

NTSTATUS
LogRecovery::ResetLog()
{
    PDataRun Runs = LogFile->FindNonResidentData(LogData);
    PUCHAR Fill = new(PagedPool, TAG_LOG_FILE_SERVICE) UCHAR[ResetChunkSize];
    ULONG BytesPerCluster = DiskVolume->BytesPerSector * (ULONG)DiskVolume->SectorsPerCluster;
    ULONGLONG Remaining = StreamSize;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Runs || !Fill)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlFillMemory(Fill, ResetChunkSize, 0xff);

    for (PDataRun Run = Runs; Run && Remaining; Run = Run->NextRun)
    {
        ULONGLONG Offset = Run->LCN * BytesPerCluster;
        ULONGLONG Bytes = min(Run->Length * BytesPerCluster, Remaining);

        if (Run->IsSparse)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        Remaining -= Bytes;
        while (Bytes)
        {
            ULONG Chunk = (ULONG)min(Bytes, (ULONGLONG)ResetChunkSize);

            Status = DiskVolume->WriteVolume(Offset, Chunk, Fill);
            if (!NT_SUCCESS(Status))
                goto Done;
            Offset += Chunk;
            Bytes -= Chunk;
        }
    }
    if (Remaining)
        Status = STATUS_FILE_CORRUPT_ERROR;
    else
        Status = DiskVolume->FlushVolume();

Done:
    FreeDataRun(Runs);
    delete[] Fill;
    return Status;
}

NTSTATUS
LogRecovery::SetVolumeDirty(_In_ BOOLEAN Dirty,
                            _Inout_ PBOOLEAN WasDirty)
{
    ULONGLONG Lcns[MaximumRecordClusters];
    ULONG RecordOffset = VolumeRecordNumber * FileRecordSize;
    ULONG Count = (FileRecordSize + ClusterSize - 1) / ClusterSize;
    LogAction Action = {};
    NTSTATUS Status;

    if (Count > MaximumRecordClusters)
        return STATUS_FILE_SYSTEM_LIMITATION;
    for (ULONG Index = 0; Index < Count; Index++)
        Lcns[Index] = MftLcn + RecordOffset / ClusterSize + Index;
    Action.Lcns = Lcns;
    Action.LcnCount = (USHORT)Count;
    Action.ClusterBlockOffset = (USHORT)((RecordOffset % ClusterSize) / LogBlockSize);

    for (ULONG Image = 0; Image < 2; Image++)
    {
        PFileRecordHeader Header;
        BOOLEAN Found = FALSE;
        ULONG Offset;

        for (ULONG Index = 0; Index < Count; Index++)
        {
            LcnSlot* Slot;

            if (!Cluster(Lcns[Index]))
                return STATUS_INSUFFICIENT_RESOURCES;
            Slot = Clusters.Find(Lcns[Index]);
            if (Image == 1 && Slot->RedoData)
            {
                PUCHAR Saved = Slot->Data;

                Slot->Data = Slot->RedoData;
                Slot->RedoData = Saved;
                Found = TRUE;
            }
        }
        if (Image == 1 && !Found)
            break;

        Status = LoadBlock(Action, Action.ClusterBlockOffset * LogBlockSize, FileRecordSize);
        if (NT_SUCCESS(Status) &&
            (RtlCompareMemory(Block, "FILE", 4) != 4 ||
             !NT_SUCCESS(NtfsApplyFixup(reinterpret_cast<PNTFSRecordHeader>(Block),
                                        FileRecordSize,
                                        DiskVolume->BytesPerSector))))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
        }

        Header = reinterpret_cast<PFileRecordHeader>(Block);
        Found = FALSE;
        for (Offset = Header->AttributeOffset;
             NT_SUCCESS(Status) && Offset + ResidentHeaderLength <= Header->ActualSize &&
             Header->ActualSize <= FileRecordSize;)
        {
            PAttribute Attr = reinterpret_cast<PAttribute>(Block + Offset);

            if (Attr->AttributeType == TypeAttributeEndMarker || Attr->Length == 0 ||
                Attr->Length > Header->ActualSize - Offset)
            {
                break;
            }
            if (Attr->AttributeType == TypeVolumeInformation && !Attr->IsNonResident &&
                Attr->Resident.DataOffset + sizeof(VolumeInformationEx) <= Attr->Length)
            {
                PVolumeInformationEx Info =
                    reinterpret_cast<PVolumeInformationEx>(Block + Offset + Attr->Resident.DataOffset);

                if (Image == 0 && Dirty)
                    *WasDirty = (Info->Flags & VolumeIsDirty) != 0;
                if (Dirty)
                    Info->Flags |= VolumeIsDirty;
                else
                    Info->Flags &= ~VolumeIsDirty;
                Found = TRUE;
                break;
            }
            Offset += Attr->Length;
        }
        if (NT_SUCCESS(Status) && !Found)
            Status = STATUS_FILE_CORRUPT_ERROR;
        if (NT_SUCCESS(Status))
        {
            Status = NtfsCommitFixup(reinterpret_cast<PNTFSRecordHeader>(Block),
                                     FileRecordSize,
                                     DiskVolume->BytesPerSector);
        }
        if (NT_SUCCESS(Status))
            Status = StoreBlock(Action, Action.ClusterBlockOffset * LogBlockSize, FileRecordSize);

        if (Image == 1)
        {
            for (ULONG Index = 0; Index < Count; Index++)
            {
                LcnSlot* Slot = Clusters.Find(Lcns[Index]);

                if (Slot->RedoData)
                {
                    PUCHAR Saved = Slot->Data;

                    Slot->Data = Slot->RedoData;
                    Slot->RedoData = Saved;
                }
            }
        }
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
LogRecovery::WriteClusters(_In_ BOOLEAN UndoState,
                           _Inout_ PBOOLEAN Modified)
{
    ULONGLONG MftStart = MftLcn * ClusterSize;
    ULONG MirrorBytes = 4 * FileRecordSize;
    BOOLEAN MirrorStale = FALSE;
    NTSTATUS Status;

    for (ULONG Index = 0; Index < Clusters.Capacity; Index++)
    {
        LcnSlot* Slot = &Clusters.Slots[Index];
        ULONGLONG Offset;
        PUCHAR Data;

        if (!Slot->Used || !(UndoState ? Slot->UndoDirty : Slot->Dirty))
            continue;

        Data = !UndoState && Slot->RedoData ? Slot->RedoData : Slot->Data;
        Offset = Slot->Lcn * ClusterSize;
        Status = DiskVolume->WriteVolume(Offset, ClusterSize, Data);
        if (!NT_SUCCESS(Status))
            return Status;
        if (UndoState)
            Slot->UndoDirty = FALSE;
        else
            Slot->Dirty = FALSE;
        *Modified = TRUE;
        if (Offset < MftStart + MirrorBytes && Offset + ClusterSize > MftStart)
            MirrorStale = TRUE;
    }

    if (MirrorStale)
    {
        PUCHAR Mirror = new(PagedPool, TAG_LOG_FILE_SERVICE) UCHAR[MirrorBytes];

        if (!Mirror)
            return STATUS_INSUFFICIENT_RESOURCES;
        Status = DiskVolume->ReadVolume(MftStart, MirrorBytes, Mirror);
        if (NT_SUCCESS(Status))
            Status = DiskVolume->WriteVolume(MftMirrorLcn * ClusterSize, MirrorBytes, Mirror);
        delete[] Mirror;
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return DiskVolume->FlushVolume();
}

NTSTATUS
LogRecovery::Run(_Out_ PBOOLEAN Modified)
{
    BOOLEAN WasDirty = FALSE;
    NTSTATUS Status;

    *Modified = FALSE;
    Status = Prepare();
    if (!NT_SUCCESS(Status))
        return Status;
    if (ClientRestartLsn == 0)
        return ResetLog();

    Status = Analyze();
    if (NT_SUCCESS(Status))
        Status = Redo();
    if (NT_SUCCESS(Status))
        Status = Undo();
    if (NT_SUCCESS(Status) && UndoneCount)
        Status = SetVolumeDirty(TRUE, &WasDirty);
    if (NT_SUCCESS(Status))
        Status = WriteClusters(FALSE, Modified);
    if (NT_SUCCESS(Status))
        Status = ResetLog();
    if (!NT_SUCCESS(Status) || !UndoneCount)
        goto Done;

    Status = WriteClusters(TRUE, Modified);
    for (ULONG Index = 0; Index < Clusters.Capacity; Index++)
    {
        delete[] Clusters.Slots[Index].RedoData;
        Clusters.Slots[Index].RedoData = NULL;
    }
    if (NT_SUCCESS(Status) && !WasDirty)
    {
        Status = SetVolumeDirty(FALSE, &WasDirty);
        if (NT_SUCCESS(Status))
            Status = WriteClusters(FALSE, Modified);
        if (NT_SUCCESS(Status))
            Status = WriteClusters(TRUE, Modified);
    }

Done:
    if (NT_SUCCESS(Status))
    {
        DPRINT1("Log replayed up to %08lx%08lx: %lu operation(s) redone, %lu undone\n",
                (ULONG)(LastLsn >> 32), (ULONG)LastLsn, AppliedCount, UndoneCount);
    }
    return Status;
}

}

NTSTATUS
LogFileService::PerformFileSystemRecovery()
{
    PAttribute FileDataAttr = LogFile->GetAttribute(TypeData, NULL);
    LogRecovery* Recovery;
    NTSTATUS Status;

    if (!FileDataAttr)
        return STATUS_NOT_FOUND;

    Recovery = new(PagedPool, TAG_LOG_FILE_SERVICE) LogRecovery(DiskVolume,
                                                                LogFile,
                                                                FileDataAttr,
                                                                SelectedRestartPage,
                                                                GetAttributeDataSize(FileDataAttr));
    if (!Recovery)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = SelectedRestartPage ? Recovery->Run(&VolumeRecovered) : Recovery->ResetLog();
    delete Recovery;
    return Status;
}
