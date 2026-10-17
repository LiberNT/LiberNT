/*
 * PROJECT:     LiberNT NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Volume consistency check
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define CHECK_RECORD_CHUNK      64
#define CHECK_BITMAP_CHUNK      0x10000
#define CHECK_FIRST_USER_RECORD 24

namespace
{

struct CrossLink
{
    ULONGLONG Owner;
    ULONGLONG Vcn;
    ULONGLONG Length;
    ULONG Type;
    USHORT NameLength;
    WCHAR Name[256];
};

struct CorruptRecord
{
    ULONGLONG Number;
    ULONG Faults;
    BOOLEAN Readable;
};

struct DanglingEntry
{
    ULONGLONG Directory;
    ULONGLONG Target;
    USHORT NameLength;
    WCHAR Name[255];
};

struct LostFile
{
    ULONGLONG Number;
    ULONGLONG Parent;
    USHORT NameLength;
    WCHAR Name[NTFS_MAX_FILE_NAME_LENGTH + 1];
};

struct NameTally
{
    ULONG ExpectedHash;
    ULONG FoundHash;
    USHORT Expected;
    USHORT Found;
};

class VolumeCheck
{
public:
    VolumeCheck(_In_ PVolume TargetVolume,
                _In_opt_ PNtfsCheckCallback Callback,
                _In_opt_ void* Context,
                _Out_ PNtfsCheckResult Result)
        : V(TargetVolume), Report(Callback), ReportContext(Context), Out(Result)
    {
    }

    ~VolumeCheck()
    {
        delete[] UsedMap;
        delete[] MftBitmap;
        delete[] DirectoryMap;
        delete[] InUseMap;
        delete[] Sequence;
        delete[] Names;
        delete[] Records;
        delete[] Dangling;
        delete[] Corrupt;
        delete[] Orphans;
        delete[] Lost;
        delete[] CrossLinks;
        delete[] NameKeys;
        delete[] ExtensionOwner;
        delete[] AttrListMap;
    }

    NTSTATUS Run(_In_ BOOLEAN Repair);

private:
    PVolume V;
    PNtfsCheckCallback Report;
    void* ReportContext;
    PNtfsCheckResult Out;

    ULONG ClusterSize = 0;
    ULONG RecordSize = 0;
    ULONGLONG ClusterCount = 0;
    ULONGLONG RecordCount = 0;
    ULONGLONG StructuralErrors = 0;
    BOOLEAN RepairIndexes = FALSE;
    BOOLEAN RepairRecords = FALSE;
    BOOLEAN MftBitmapChanged = FALSE;
    ULONG MftBitmapFixes = 0;
    ULONG RecordFaults = 0;
    CorruptRecord* Corrupt = NULL;
    ULONG CorruptCount = 0;
    ULONG CorruptCapacity = 0;
    ULONGLONG* Orphans = NULL;
    CrossLink* CrossLinks = NULL;
    ULONG CrossLinkCount = 0;
    ULONG CrossLinkCapacity = 0;
    ULONG OrphanCount = 0;
    ULONG OrphanCapacity = 0;
    LostFile* Lost = NULL;
    ULONG LostCount = 0;
    ULONG LostCapacity = 0;
    ULONGLONG* NameKeys = NULL;
    ULONG NameKeyCount = 0;
    ULONG NameKeyCapacity = 0;
    BOOLEAN NameKeysIncomplete = FALSE;
    PULONG ExtensionOwner = NULL;
    PUCHAR AttrListMap = NULL;
    DanglingEntry* Dangling = NULL;
    ULONG DanglingCount = 0;
    ULONG DanglingCapacity = 0;

    PUCHAR UsedMap = NULL;
    PUCHAR MftBitmap = NULL;
    ULONG MftBitmapLength = 0;
    PUCHAR DirectoryMap = NULL;
    PUCHAR InUseMap = NULL;
    PUSHORT Sequence = NULL;
    NameTally* Names = NULL;
    PUCHAR Records = NULL;

    static BOOLEAN TestBit(_In_ const UCHAR* Map, _In_ ULONGLONG Bit)
    {
        return (Map[Bit >> 3] & (1 << (Bit & 7))) != 0;
    }

    static void SetBit(_Inout_ PUCHAR Map, _In_ ULONGLONG Bit)
    {
        Map[Bit >> 3] |= (UCHAR)(1 << (Bit & 7));
    }

    static PUCHAR AllocateMap(_In_ ULONGLONG Bits)
    {
        ULONGLONG Bytes = (Bits + 7) / 8;
        PUCHAR Map;

        if (Bytes == 0 || Bytes > MAXULONG)
            return NULL;
        Map = new(PagedPool, TAG_NTFS) UCHAR[(ULONG)Bytes];
        if (Map)
            RtlZeroMemory(Map, (ULONG)Bytes);
        return Map;
    }

    static ULONG HashName(_In_ const FileNameEx* Name)
    {
        const UCHAR* Bytes = reinterpret_cast<const UCHAR*>(Name);
        ULONG Hash = 2166136261u;
        ULONG Length = FIELD_OFFSET(FileNameEx, Name) + Name->NameLength * sizeof(WCHAR);

        for (ULONG Index = 0; Index < sizeof(Name->ParentFileReference); Index++)
            Hash = (Hash ^ Bytes[Index]) * 16777619u;
        for (ULONG Index = FIELD_OFFSET(FileNameEx, NameLength); Index < Length; Index++)
            Hash = (Hash ^ Bytes[Index]) * 16777619u;
        return Hash;
    }

    void Event(_In_ NtfsCheckEvent Kind, _In_ ULONGLONG Arg1, _In_ ULONGLONG Arg2)
    {
        if (Kind != NtfsCheckLostClustersFreed && Kind != NtfsCheckUsedClustersClaimed &&
            Kind != NtfsCheckIndexEntryFixed && Kind != NtfsCheckIndexEntryRemoved &&
            Kind != NtfsCheckRecordFreed && Kind != NtfsCheckRecordBitmapFixed &&
            Kind != NtfsCheckNameRelinked && Kind != NtfsCheckCrossLinkResolved &&
            Kind != NtfsCheckLostAndFound && Kind != NtfsCheckFileRecovered &&
            Kind != NtfsCheckAttributeListFixed && Kind != NtfsCheckDataAttributeInserted &&
            Kind != NtfsCheckLostNameRemoved)
        {
            Out->Errors++;
        }
        if (Kind == NtfsCheckRecordCorrupt || Kind == NtfsCheckBadRunList ||
            Kind == NtfsCheckClusterOutOfRange || Kind == NtfsCheckCrossLinked)
        {
            StructuralErrors++;
        }
        if (Kind == NtfsCheckRecordCorrupt || Kind == NtfsCheckBadRunList ||
            Kind == NtfsCheckClusterOutOfRange)
        {
            RecordFaults++;
        }
        if (Report)
            Report(ReportContext, Kind, Arg1, Arg2);
    }

    NTSTATUS LoadMftBitmap();
    NTSTATUS ReadRecords(_In_ ULONGLONG First, _In_ ULONG Count);
    void CheckRecord(_In_ ULONGLONG Number, _Inout_ PUCHAR Record);
    void CheckAttributes(_In_ ULONGLONG Number, _In_ ULONGLONG Owner, _In_ PUCHAR Record);
    void CheckRunList(_In_ ULONGLONG Number, _In_ ULONGLONG Owner, _In_ PAttribute Attr);
    struct ClusterRange
    {
        ULONGLONG Lcn;
        ULONGLONG Length;
    };

    struct RangeList
    {
        ClusterRange* Ranges;
        ULONG Count;
        ULONG Capacity;
    };

    static BOOLEAN AddRange(_Inout_ RangeList* List, _In_ ULONGLONG Lcn);
    NTSTATUS CheckBitmap(_In_ BOOLEAN Repair);
    NTSTATUS RepairRange(_In_ BOOLEAN Claim, _In_ ULONGLONG Lcn, _In_ ULONGLONG Count);
    BOOLEAN RecordHasName(_In_ ULONGLONG Ordinal, _In_ const FileNameEx* Key, _In_ ULONG KeyLength);
    NTSTATUS WriteStreamRange(_In_ PDataRun Runs, _In_ ULONGLONG Offset, _In_ ULONG Length, _In_ PUCHAR Data);
    void RememberDangling(_In_ ULONGLONG Directory, _In_ ULONGLONG Target, _In_ const FileNameEx* Key);
    void RememberCorrupt(_In_ ULONGLONG Number, _In_ ULONG Faults, _In_ BOOLEAN Readable);
    void RememberOrphan(_In_ ULONGLONG Number);
    NTSTATUS WriteMftBitmap();
    void RepairRecordsPass();
    void RelinkOrphans();
    void RememberLost(_In_ ULONGLONG Number, _In_ const FileNameEx* Name);
    void RememberNameKey(_In_ ULONGLONG Owner, _In_ ULONG Hash);
    void SortNameKeys();
    BOOLEAN HasNameKey(_In_ ULONGLONG Owner, _In_ ULONG Hash);
    static BOOLEAN KeepListEntry(_In_ void* Context, _In_ const AttributeListEx* Entry);
    void CheckAttributeLists();
    void RecoverLostFiles();
    void RememberCrossLink(_In_ ULONGLONG Owner, _In_ PAttribute Attr, _In_ ULONGLONG Vcn, _In_ ULONGLONG Length);
    void ResolveCrossLinks();
    void RemoveDangling();
    ULONG CheckIndexEntries(_In_ ULONGLONG Directory,
                            _In_ PIndexNodeHeader Node,
                            _In_ ULONG Available,
                            _In_ ULONGLONG Vcn,
                            _In_ BOOLEAN AllowFix);
    void CheckDirectory(_In_ ULONGLONG Directory);
};

NTSTATUS
VolumeCheck::LoadMftBitmap()
{
    PFileRecord MftFile;
    PAttribute BitmapAttr;
    NTSTATUS Status;

    Status = V->MFT->GetFileAttributeFromFileRecordNumber(TypeBitmap, NULL, _MFT, &MftFile, &BitmapAttr);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = MftFile->ReadAttributeAlloc(BitmapAttr, &MftBitmap, &MftBitmapLength);
    delete MftFile;
    if (NT_SUCCESS(Status) && (ULONGLONG)MftBitmapLength * 8 < RecordCount)
        Status = STATUS_FILE_CORRUPT_ERROR;
    return Status;
}

NTSTATUS
VolumeCheck::ReadRecords(_In_ ULONGLONG First, _In_ ULONG Count)
{
    ULONG Index = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    while (NT_SUCCESS(Status) && Index < Count)
    {
        ULONGLONG Start, Next;
        ULONG Run = 1;

        Status = V->MFT->GetFileRecordDiskOffset(First + Index, &Start);
        while (NT_SUCCESS(Status) && Index + Run < Count &&
               NT_SUCCESS(V->MFT->GetFileRecordDiskOffset(First + Index + Run, &Next)) &&
               Next == Start + (ULONGLONG)Run * RecordSize)
        {
            Run++;
        }
        if (NT_SUCCESS(Status))
            Status = V->ReadVolume(Start, Run * RecordSize, Records + Index * RecordSize);
        Index += Run;
    }
    return Status;
}

void
VolumeCheck::CheckRunList(_In_ ULONGLONG Number, _In_ ULONGLONG Owner, _In_ PAttribute Attr)
{
    const UCHAR* Run;
    const UCHAR* End;
    ULONGLONG Vcn = Attr->NonResident.FirstVCN;
    ULONGLONG Lcn = 0;
    BOOLEAN Reported = FALSE;

    if (Attr->NonResident.DataRunsOffset >= Attr->Length)
    {
        Event(NtfsCheckBadRunList, Number, Attr->AttributeType);
        return;
    }

    Run = reinterpret_cast<const UCHAR*>(Attr) + Attr->NonResident.DataRunsOffset;
    End = reinterpret_cast<const UCHAR*>(Attr) + Attr->Length;
    while (Run < End && *Run)
    {
        ULONG LengthBytes = *Run & 0x0F;
        ULONG OffsetBytes = *Run >> 4;
        ULONGLONG Length = 0;
        LONGLONG Delta = 0;

        Run++;
        if (LengthBytes == 0 || LengthBytes > 8 || OffsetBytes > 8 ||
            (ULONG)(End - Run) < LengthBytes + OffsetBytes)
        {
            Event(NtfsCheckBadRunList, Number, Attr->AttributeType);
            return;
        }
        for (ULONG Index = 0; Index < LengthBytes; Index++)
            Length |= (ULONGLONG)Run[Index] << (Index * 8);
        Run += LengthBytes;
        if (OffsetBytes)
        {
            for (ULONG Index = 0; Index < OffsetBytes; Index++)
                Delta |= (LONGLONG)((ULONGLONG)Run[Index] << (Index * 8));
            if (Run[OffsetBytes - 1] & 0x80)
                Delta |= (LONGLONG)(~0ULL << (OffsetBytes * 8 - 1) << 1);
            Run += OffsetBytes;
            Lcn += (ULONGLONG)Delta;
            if (Lcn >= ClusterCount || Length > ClusterCount - Lcn)
            {
                Event(NtfsCheckClusterOutOfRange, Number, Lcn);
                return;
            }
            for (ULONGLONG Cluster = Lcn; Cluster < Lcn + Length; Cluster++)
            {
                if (TestBit(UsedMap, Cluster))
                {
                    if (!Reported)
                    {
                        Event(NtfsCheckCrossLinked, Number, Cluster);
                        RememberCrossLink(Owner, Attr, Vcn, Length);
                    }
                    Reported = TRUE;
                    continue;
                }
                SetBit(UsedMap, Cluster);
                Out->UsedClusters++;
            }
        }
        Vcn += Length;
    }

    if (Vcn != Attr->NonResident.LastVCN + 1)
        Event(NtfsCheckBadRunList, Number, Attr->AttributeType);
}

void
VolumeCheck::CheckAttributes(_In_ ULONGLONG Number, _In_ ULONGLONG Owner, _In_ PUCHAR Record)
{
    PFileRecordHeader Header = reinterpret_cast<PFileRecordHeader>(Record);
    ULONG Offset = Header->AttributeOffset;

    while (Offset + sizeof(ULONG) <= Header->ActualSize)
    {
        PAttribute Attr = reinterpret_cast<PAttribute>(Record + Offset);

        if (Attr->AttributeType == TypeAttributeEndMarker)
            return;
        if (Offset + FIELD_OFFSET(Attribute, Resident) > Header->ActualSize ||
            Attr->Length < FIELD_OFFSET(Attribute, Resident) + 8 || (Attr->Length & 7) != 0 ||
            Attr->Length > Header->ActualSize - Offset ||
            (Attr->NameLength && (ULONG)Attr->NameOffset + Attr->NameLength * sizeof(WCHAR) > Attr->Length))
        {
            Event(NtfsCheckRecordCorrupt, Number, Offset);
            return;
        }

        if (Attr->AttributeType == TypeAttributeList && Owner == Number)
            SetBit(AttrListMap, Number);

        if (Attr->IsNonResident)
        {
            if (Attr->Length < FIELD_OFFSET(Attribute, NonResident.CompressedDataSize))
            {
                Event(NtfsCheckRecordCorrupt, Number, Offset);
                return;
            }
            CheckRunList(Number, Owner, Attr);
        }
        else
        {
            if ((ULONG)Attr->Resident.DataOffset + Attr->Resident.DataLength > Attr->Length)
            {
                Event(NtfsCheckRecordCorrupt, Number, Offset);
                return;
            }
            if (Attr->AttributeType == TypeFileName)
            {
                const FileNameEx* Name =
                    reinterpret_cast<const FileNameEx*>(reinterpret_cast<PUCHAR>(Attr) + Attr->Resident.DataOffset);

                if (Attr->Resident.DataLength < FIELD_OFFSET(FileNameEx, Name) ||
                    Attr->Resident.DataLength < FIELD_OFFSET(FileNameEx, Name) + Name->NameLength * sizeof(WCHAR))
                {
                    Event(NtfsCheckRecordCorrupt, Number, Offset);
                    return;
                }
                Names[Owner].Expected++;
                Names[Owner].ExpectedHash += HashName(Name);
                RememberNameKey(Owner, HashName(Name));
            }
            else if (Attr->AttributeType == TypeIndexRoot && Attr->NameLength == 4 &&
                     RtlCompareMemory(reinterpret_cast<PUCHAR>(Attr) + Attr->NameOffset, L"$I30", 8) == 8)
            {
                SetBit(DirectoryMap, Owner);
            }
        }
        Offset += Attr->Length;
    }

    Event(NtfsCheckRecordCorrupt, Number, Offset);
}

void
VolumeCheck::CheckRecord(_In_ ULONGLONG Number, _Inout_ PUCHAR Record)
{
    PFileRecordHeader Header = reinterpret_cast<PFileRecordHeader>(Record);
    BOOLEAN Allocated = TestBit(MftBitmap, Number);
    BOOLEAN InUse;
    ULONGLONG Owner;
    ULONG Faults = RecordFaults;

    if (RtlCompareMemory(Header->Header.TypeID, "FILE", 4) != 4 ||
        !NT_SUCCESS(NtfsApplyFixup(&Header->Header, RecordSize, V->BytesPerSector)))
    {
        if (Allocated && Number >= CHECK_FIRST_USER_RECORD)
        {
            Event(NtfsCheckRecordCorrupt, Number, 0);
            RememberCorrupt(Number, RecordFaults - Faults, FALSE);
        }
        return;
    }

    InUse = (Header->Flags & FR_IN_USE) != 0;
    if (InUse != Allocated && (InUse || Number >= CHECK_FIRST_USER_RECORD))
    {
        Event(NtfsCheckRecordBitmap, Number, InUse);
        if (RepairRecords)
        {
            if (InUse)
                SetBit(MftBitmap, Number);
            else
                MftBitmap[Number >> 3] &= (UCHAR)~(1 << (Number & 7));
            MftBitmapChanged = TRUE;
            MftBitmapFixes++;
        }
    }
    if (!InUse)
        return;

    if (Header->AllocatedSize != RecordSize || Header->ActualSize > RecordSize ||
        Header->AttributeOffset < sizeof(NTFSRecordHeader) || (Header->AttributeOffset & 7) != 0 ||
        Header->AttributeOffset >= Header->ActualSize ||
        (Header->Header.UpdateSequenceOffset >= FIELD_OFFSET(FileRecordHeader, MFTRecordNumber) + sizeof(UINT32) &&
         Header->MFTRecordNumber != (UINT32)Number))
    {
        Event(NtfsCheckRecordCorrupt, Number, 0);
        if (Number >= CHECK_FIRST_USER_RECORD)
            RememberCorrupt(Number, RecordFaults - Faults, TRUE);
        return;
    }

    Owner = Header->BaseFileRecord & 0x0000FFFFFFFFFFFFULL;
    if (Owner == 0)
    {
        Owner = Number;
        SetBit(InUseMap, Number);
        Sequence[Number] = Header->SequenceNumber;
        Out->FileRecords++;
    }
    else if (Owner >= RecordCount)
    {
        Event(NtfsCheckRecordCorrupt, Number, 0);
        return;
    }

    CheckAttributes(Number, Owner, Record);
    if (RecordFaults == Faults && Owner != Number)
    {
        ExtensionOwner[Number] = (ULONG)Owner + 1;
        Sequence[Number] = Header->SequenceNumber;
    }
    if (RecordFaults != Faults && Owner == Number && Number >= CHECK_FIRST_USER_RECORD)
    {
        InUseMap[Number >> 3] &= (UCHAR)~(1 << (Number & 7));
        DirectoryMap[Number >> 3] &= (UCHAR)~(1 << (Number & 7));
        Names[Number].Expected = Names[Number].Found = 0;
        Names[Number].ExpectedHash = Names[Number].FoundHash = 0;
        Out->FileRecords--;
        RememberCorrupt(Number, RecordFaults - Faults, TRUE);
    }
}

void
VolumeCheck::RememberCorrupt(_In_ ULONGLONG Number, _In_ ULONG Faults, _In_ BOOLEAN Readable)
{
    if (!RepairRecords)
        return;
    if (CorruptCount == CorruptCapacity)
    {
        ULONG Capacity = CorruptCapacity ? CorruptCapacity * 2 : 16;
        CorruptRecord* Grown = new(PagedPool, TAG_NTFS) CorruptRecord[Capacity];

        if (!Grown)
            return;
        if (Corrupt)
            RtlCopyMemory(Grown, Corrupt, CorruptCount * sizeof(CorruptRecord));
        delete[] Corrupt;
        Corrupt = Grown;
        CorruptCapacity = Capacity;
    }
    Corrupt[CorruptCount].Number = Number;
    Corrupt[CorruptCount].Faults = Faults;
    Corrupt[CorruptCount].Readable = Readable;
    CorruptCount++;
}

void
VolumeCheck::RememberCrossLink(_In_ ULONGLONG Owner, _In_ PAttribute Attr, _In_ ULONGLONG Vcn, _In_ ULONGLONG Length)
{
    if (!RepairRecords || Owner < CHECK_FIRST_USER_RECORD || Length == 0 || Length > MAXULONG)
        return;
    if (CrossLinkCount == CrossLinkCapacity)
    {
        ULONG Capacity = CrossLinkCapacity ? CrossLinkCapacity * 2 : 8;
        CrossLink* Grown = new(PagedPool, TAG_NTFS) CrossLink[Capacity];

        if (!Grown)
            return;
        if (CrossLinks)
            RtlCopyMemory(Grown, CrossLinks, CrossLinkCount * sizeof(CrossLink));
        delete[] CrossLinks;
        CrossLinks = Grown;
        CrossLinkCapacity = Capacity;
    }

    CrossLinks[CrossLinkCount].Owner = Owner;
    CrossLinks[CrossLinkCount].Vcn = Vcn;
    CrossLinks[CrossLinkCount].Length = Length;
    CrossLinks[CrossLinkCount].Type = Attr->AttributeType;
    CrossLinks[CrossLinkCount].NameLength = Attr->NameLength;
    RtlCopyMemory(CrossLinks[CrossLinkCount].Name,
                  reinterpret_cast<PUCHAR>(Attr) + Attr->NameOffset,
                  Attr->NameLength * sizeof(WCHAR));
    CrossLinks[CrossLinkCount].Name[Attr->NameLength] = 0;
    CrossLinkCount++;
}

void
VolumeCheck::ResolveCrossLinks()
{
    for (ULONG Index = 0; Index < CrossLinkCount; Index++)
    {
        CrossLink* Link = &CrossLinks[Index];
        PDataRun Target = NULL;
        PFileRecord File;
        ULONGLONG Lcn;
        NTSTATUS Status;

        Status = V->AllocateClusters(0, (ULONG)Link->Length, 1, &Target);
        if (!NT_SUCCESS(Status) || !Target || Target->Length != Link->Length || Target->NextRun)
        {
            if (Target)
            {
                V->ReleaseClusters(Target);
                FreeDataRun(Target);
            }
            continue;
        }
        Lcn = Target->LCN;
        V->ReleaseClusters(Target);
        FreeDataRun(Target);

        if (!NT_SUCCESS(V->MFT->GetFileRecord((ULONG)Link->Owner, &File)))
            continue;
        Status = File->MoveClusters((AttributeType)Link->Type,
                                    Link->NameLength ? Link->Name : NULL,
                                    Link->Vcn,
                                    Lcn,
                                    (ULONG)Link->Length);
        delete File;

        if (NT_SUCCESS(Status))
        {
            Out->Errors--;
            StructuralErrors--;
            Out->ResolvedCrossLinks++;
            Event(NtfsCheckCrossLinkResolved, Link->Owner, Lcn);
        }
    }
    if (CrossLinkCount)
        V->FlushVolume();
}

void
VolumeCheck::RememberOrphan(_In_ ULONGLONG Number)
{
    if (OrphanCount == OrphanCapacity)
    {
        ULONG Capacity = OrphanCapacity ? OrphanCapacity * 2 : 16;
        ULONGLONG* Grown = new(PagedPool, TAG_NTFS) ULONGLONG[Capacity];

        if (!Grown)
            return;
        if (Orphans)
            RtlCopyMemory(Grown, Orphans, OrphanCount * sizeof(ULONGLONG));
        delete[] Orphans;
        Orphans = Grown;
        OrphanCapacity = Capacity;
    }
    Orphans[OrphanCount++] = Number;
}

NTSTATUS
VolumeCheck::WriteMftBitmap()
{
    PFileRecord MftFile;
    PAttribute BitmapAttr;
    PDataRun Runs;
    NTSTATUS Status;

    Status = V->MFT->GetFileAttributeFromFileRecordNumber(TypeBitmap, NULL, _MFT, &MftFile, &BitmapAttr);
    if (!NT_SUCCESS(Status))
        return Status;

    if (BitmapAttr->IsNonResident)
    {
        Runs = MftFile->FindNonResidentData(BitmapAttr);
        Status = Runs ? WriteStreamRange(Runs, 0, MftBitmapLength, MftBitmap) : STATUS_INSUFFICIENT_RESOURCES;
        if (Runs)
            FreeDataRun(Runs);
    }
    else
    {
        Status = MftFile->ReplaceResidentData(BitmapAttr, MftBitmap, MftBitmapLength);
        if (NT_SUCCESS(Status))
            Status = V->MFT->WriteFileRecordToMFT(MftFile);
    }
    delete MftFile;

    if (NT_SUCCESS(Status))
        Status = V->FlushVolume();
    return Status;
}

void
VolumeCheck::RepairRecordsPass()
{
    PUCHAR Record = new(PagedPool, TAG_NTFS) UCHAR[RecordSize];
    PFileRecordHeader Header = reinterpret_cast<PFileRecordHeader>(Record);

    if (!Record)
        return;

    for (ULONG Index = 0; Index < CorruptCount; Index++)
    {
        ULONGLONG Number = Corrupt[Index].Number;
        ULONGLONG DiskOffset;
        BOOLEAN Freed = TRUE;

        if (Corrupt[Index].Readable)
        {
            Freed = NT_SUCCESS(V->MFT->GetFileRecordDiskOffset(Number, &DiskOffset)) &&
                    NT_SUCCESS(V->ReadVolume(DiskOffset, RecordSize, Record)) &&
                    RtlCompareMemory(Header->Header.TypeID, "FILE", 4) == 4 &&
                    NT_SUCCESS(NtfsApplyFixup(&Header->Header, RecordSize, V->BytesPerSector));
            if (Freed)
            {
                Header->Flags &= ~FR_IN_USE;
                Freed = NT_SUCCESS(NtfsCommitFixup(&Header->Header, RecordSize, V->BytesPerSector)) &&
                        NT_SUCCESS(V->WriteVolume(DiskOffset, RecordSize, Record));
            }
        }
        if (!Freed)
            continue;

        MftBitmap[Number >> 3] &= (UCHAR)~(1 << (Number & 7));
        MftBitmapChanged = TRUE;
        Out->FreedRecords++;
        Out->Errors -= Corrupt[Index].Faults;
        StructuralErrors -= Corrupt[Index].Faults;
        Event(NtfsCheckRecordFreed, Number, 0);
    }
    delete[] Record;

    if (MftBitmapChanged && NT_SUCCESS(WriteMftBitmap()))
    {
        Out->Errors -= MftBitmapFixes;
        if (MftBitmapFixes)
            Event(NtfsCheckRecordBitmapFixed, MftBitmapFixes, 0);
    }
}

void
VolumeCheck::RelinkOrphans()
{
    PUCHAR Record = new(PagedPool, TAG_NTFS) UCHAR[RecordSize];
    PFileRecordHeader Header = reinterpret_cast<PFileRecordHeader>(Record);
    WCHAR NameBuffer[256];

    if (!Record)
        return;

    for (ULONG Index = 0; Index < OrphanCount; Index++)
    {
        ULONGLONG Number = Orphans[Index];
        ULONGLONG DiskOffset;
        ULONG Offset, Relinked = 0, NameCount = 0, DeadNames = 0, DeadParentCount = 0;
        ULONGLONG DeadParents[8];
        PFileNameEx DeadName = NULL;
        BOOLEAN Failed = FALSE, OtherFailures = FALSE;

        if (!NT_SUCCESS(V->MFT->GetFileRecordDiskOffset(Number, &DiskOffset)) ||
            !NT_SUCCESS(V->ReadVolume(DiskOffset, RecordSize, Record)) ||
            RtlCompareMemory(Header->Header.TypeID, "FILE", 4) != 4 ||
            !NT_SUCCESS(NtfsApplyFixup(&Header->Header, RecordSize, V->BytesPerSector)) ||
            Header->ActualSize > RecordSize)
        {
            continue;
        }

        for (Offset = Header->AttributeOffset; Offset + FIELD_OFFSET(Attribute, Resident) + 8 <= Header->ActualSize;)
        {
            PAttribute Attr = reinterpret_cast<PAttribute>(Record + Offset);
            PFileNameEx Name;
            ULONGLONG Parent;
            PFileRecord ParentFile, Found;

            if (Attr->AttributeType == TypeAttributeEndMarker || Attr->Length == 0 ||
                Attr->Length > Header->ActualSize - Offset)
            {
                break;
            }
            Offset += Attr->Length;
            if (Attr->AttributeType != TypeFileName || Attr->IsNonResident)
                continue;

            Name = reinterpret_cast<PFileNameEx>(reinterpret_cast<PUCHAR>(Attr) + Attr->Resident.DataOffset);
            Parent = Name->ParentFileReference & 0x0000FFFFFFFFFFFFULL;
            NameCount++;
            if (Parent >= RecordCount || !TestBit(InUseMap, Parent) || !TestBit(DirectoryMap, Parent) ||
                Sequence[Parent] != (USHORT)(Name->ParentFileReference >> 48))
            {
                ULONG Known;

                if (!DeadName || (DeadName->NameType == NAME_TYPE_DOS &&
                                  Name->ParentFileReference == DeadName->ParentFileReference))
                {
                    DeadName = Name;
                }
                for (Known = 0; Known < DeadParentCount && DeadParents[Known] != Name->ParentFileReference; Known++)
                {
                }
                if (Known == DeadParentCount)
                {
                    if (DeadParentCount < RTL_NUMBER_OF(DeadParents))
                        DeadParents[DeadParentCount++] = Name->ParentFileReference;
                    else
                        OtherFailures = TRUE;
                }
                DeadNames++;
                Failed = TRUE;
                continue;
            }
            if (!NT_SUCCESS(V->MFT->GetFileRecord((ULONG)Parent, &ParentFile)))
            {
                Failed = TRUE;
                OtherFailures = TRUE;
                continue;
            }

            RtlCopyMemory(NameBuffer, Name->Name, Name->NameLength * sizeof(WCHAR));
            NameBuffer[Name->NameLength] = 0;
            if (NT_SUCCESS(V->MFT->GetFileRecordInDirectory(ParentFile, NameBuffer, &Found)))
            {
                BOOLEAN Same = Found->Header->MFTRecordNumber == (UINT32)Number;

                delete Found;
                if (Same)
                {
                    delete ParentFile;
                    continue;
                }
            }

            {
                Directory ParentIndex(V);

                if (NT_SUCCESS(ParentIndex.AddFileToDirectory(ParentFile,
                                                              Number | ((ULONGLONG)Sequence[Number] << 48),
                                                              Name)))
                {
                    Relinked++;
                    Event(NtfsCheckNameRelinked, Number, Parent);
                }
                else
                {
                    Failed = TRUE;
                    OtherFailures = TRUE;
                }
            }
            delete ParentFile;
        }

        if (DeadNames && DeadNames < NameCount && !OtherFailures)
        {
            ULONG RemovedTotal = 0, Removed;
            PFileRecord File;

            if (NT_SUCCESS(V->MFT->GetFileRecord((ULONG)Number, &File)))
            {
                for (ULONG Dead = 0; Dead < DeadParentCount; Dead++)
                {
                    if (NT_SUCCESS(V->MFT->RemoveFileNamesUnderParent(File, DeadParents[Dead], &Removed)))
                        RemovedTotal += Removed;
                }
                delete File;
            }
            if (RemovedTotal == DeadNames)
            {
                Out->Errors--;
                Out->RemovedNames += RemovedTotal;
                Out->RelinkedNames += Relinked;
                Event(NtfsCheckLostNameRemoved, Number, RemovedTotal);
            }
        }
        else if (Relinked && !Failed)
        {
            Out->Errors--;
            Out->RelinkedNames += Relinked;
        }
        else if (!Relinked && DeadName && DeadNames == NameCount)
        {
            RememberLost(Number, DeadName);
        }
    }

    delete[] Record;
    if (OrphanCount)
        V->FlushVolume();
}

void
VolumeCheck::RememberNameKey(_In_ ULONGLONG Owner, _In_ ULONG Hash)
{
    if (NameKeysIncomplete)
        return;
    if (NameKeyCount == NameKeyCapacity)
    {
        ULONG Capacity = NameKeyCapacity ? NameKeyCapacity * 2 : 1024;
        ULONGLONG* Grown = Capacity > NameKeyCapacity ? new(PagedPool, TAG_NTFS) ULONGLONG[Capacity] : NULL;

        if (!Grown)
        {
            NameKeysIncomplete = TRUE;
            return;
        }
        if (NameKeys)
            RtlCopyMemory(Grown, NameKeys, NameKeyCount * sizeof(ULONGLONG));
        delete[] NameKeys;
        NameKeys = Grown;
        NameKeyCapacity = Capacity;
    }
    NameKeys[NameKeyCount++] = (Owner << 32) | Hash;
}

void
VolumeCheck::SortNameKeys()
{
    ULONG Count = NameKeyCount;

    if (Count < 2)
        return;
    for (ULONG Start = Count / 2; Start-- > 0;)
    {
        for (ULONG Root = Start; 2 * Root + 1 < Count;)
        {
            ULONG Child = 2 * Root + 1;

            if (Child + 1 < Count && NameKeys[Child] < NameKeys[Child + 1])
                Child++;
            if (NameKeys[Root] >= NameKeys[Child])
                break;
            ULONGLONG Swap = NameKeys[Root];
            NameKeys[Root] = NameKeys[Child];
            NameKeys[Child] = Swap;
            Root = Child;
        }
    }
    for (ULONG End = Count - 1; End > 0; End--)
    {
        ULONGLONG Swap = NameKeys[0];
        NameKeys[0] = NameKeys[End];
        NameKeys[End] = Swap;
        for (ULONG Root = 0; 2 * Root + 1 < End;)
        {
            ULONG Child = 2 * Root + 1;

            if (Child + 1 < End && NameKeys[Child] < NameKeys[Child + 1])
                Child++;
            if (NameKeys[Root] >= NameKeys[Child])
                break;
            Swap = NameKeys[Root];
            NameKeys[Root] = NameKeys[Child];
            NameKeys[Child] = Swap;
            Root = Child;
        }
    }
}

BOOLEAN
VolumeCheck::HasNameKey(_In_ ULONGLONG Owner, _In_ ULONG Hash)
{
    ULONGLONG Key = (Owner << 32) | Hash;
    ULONG Low = 0, High = NameKeyCount;

    if (NameKeysIncomplete)
        return TRUE;
    while (Low < High)
    {
        ULONG Middle = Low + (High - Low) / 2;

        if (NameKeys[Middle] < Key)
            Low = Middle + 1;
        else
            High = Middle;
    }
    return Low < NameKeyCount && NameKeys[Low] == Key;
}

struct ListFilterContext
{
    VolumeCheck* Check;
    ULONGLONG Base;
};

BOOLEAN
VolumeCheck::KeepListEntry(_In_ void* Context, _In_ const AttributeListEx* Entry)
{
    ListFilterContext* Filter = static_cast<ListFilterContext*>(Context);
    VolumeCheck* Check = Filter->Check;
    ULONGLONG Record = Entry->BaseFileRef & 0x0000FFFFFFFFFFFFULL;
    USHORT SequenceNumber = (USHORT)(Entry->BaseFileRef >> 48);

    if (Record == Filter->Base)
        return TRUE;
    if (Record >= Check->RecordCount || Check->ExtensionOwner[Record] != (ULONG)Filter->Base + 1)
        return FALSE;
    return SequenceNumber == 0 || Check->Sequence[Record] == SequenceNumber;
}

void
VolumeCheck::CheckAttributeLists()
{
    for (ULONGLONG Number = 0; Number < RecordCount; Number++)
    {
        ListFilterContext Filter = { this, Number };
        ULONG Dropped, DroppedNames;
        BOOLEAN DroppedData;
        PFileRecord File;
        NTSTATUS Status;

        if (!TestBit(AttrListMap, Number) || !TestBit(InUseMap, Number))
            continue;
        if (!NT_SUCCESS(V->MFT->GetFileRecord((ULONG)Number, &File)))
            continue;

        Status = File->FilterAttributeList(KeepListEntry, &Filter, FALSE, &Dropped, &DroppedNames, &DroppedData);
        if (NT_SUCCESS(Status) && Dropped)
        {
            Event(NtfsCheckAttributeListBad, Number, Dropped);
            if (RepairRecords &&
                NT_SUCCESS(File->FilterAttributeList(KeepListEntry, &Filter, TRUE, &Dropped, &DroppedNames, &DroppedData)))
            {
                Out->Errors--;
                Out->RepairedAttributeLists++;
                Event(NtfsCheckAttributeListFixed, Number, Dropped);
                if (DroppedData && !(File->Header->Flags & FR_IS_DIRECTORY))
                    Event(NtfsCheckDataAttributeInserted, Number, 0);
            }
        }
        delete File;
    }
    if (RepairRecords)
        V->FlushVolume();
}

void
VolumeCheck::RememberLost(_In_ ULONGLONG Number, _In_ const FileNameEx* Name)
{
    if (LostCount == LostCapacity)
    {
        ULONG Capacity = LostCapacity ? LostCapacity * 2 : 16;
        LostFile* Grown = new(PagedPool, TAG_NTFS) LostFile[Capacity];

        if (!Grown)
            return;
        if (Lost)
            RtlCopyMemory(Grown, Lost, LostCount * sizeof(LostFile));
        delete[] Lost;
        Lost = Grown;
        LostCapacity = Capacity;
    }
    Lost[LostCount].Number = Number;
    Lost[LostCount].Parent = Name->ParentFileReference;
    Lost[LostCount].NameLength = Name->NameLength;
    RtlCopyMemory(Lost[LostCount].Name, Name->Name, Name->NameLength * sizeof(WCHAR));
    Lost[LostCount].Name[Name->NameLength] = 0;
    LostCount++;
}

static const UCHAR LostAndFoundSecurity[] =
{
    0x01, 0x00, 0x04, 0x90, 0x48, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x14, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x34, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x14, 0x00, 0xFF, 0x01, 0x1F, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    0x12, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x18, 0x00, 0xFF, 0x01, 0x1F, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
    0x20, 0x00, 0x00, 0x00, 0x20, 0x02, 0x00, 0x00,
    0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00, 0x20, 0x02, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x12, 0x00, 0x00, 0x00
};

static void
FormatDecimal(_Out_ PWCHAR Out, _In_ ULONG Value, _In_ ULONG Digits)
{
    while (Digits--)
    {
        Out[Digits] = (WCHAR)(L'0' + Value % 10);
        Value /= 10;
    }
}

static NTSTATUS
CreateLostDirectory(_In_ PVolume V,
                    _In_ PFileRecord Parent,
                    _In_ PWCHAR Name,
                    _In_ ULONG FileAttributes,
                    _In_ UCHAR Control,
                    _Out_ PFileRecord* Directory)
{
    UCHAR Security[sizeof(LostAndFoundSecurity)];
    NTSTATUS Status;

    Status = V->MFT->CreateFileInDirectory(Parent, Name, (ULONG)NtfsWcsLen(Name), TRUE,
                                           FileAttributes, FALSE, Directory);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlCopyMemory(Security, LostAndFoundSecurity, sizeof(Security));
    Security[3] = Control;
    Status = (*Directory)->SetSecurityDescriptor(Security, sizeof(Security));
    if (!NT_SUCCESS(Status))
    {
        delete *Directory;
        *Directory = NULL;
    }
    return Status;
}

void
VolumeCheck::RecoverLostFiles()
{
    static const WCHAR Hex[] = L"0123456789ABCDEF";
    WCHAR FoundName[] = L"found.000";
    WCHAR GroupName[] = L"dir0000.chk";
    WCHAR NameBuffer[NTFS_MAX_FILE_NAME_LENGTH + 1];
    PFileRecord Root, Found = NULL, Existing;
    ULONG FoundIndex, Singles = 0, Groups = 0;

    if (!LostCount || !NT_SUCCESS(V->MFT->GetFileRecord(_Root, &Root)))
        return;

    for (FoundIndex = 0; FoundIndex < 1000; FoundIndex++)
    {
        FormatDecimal(FoundName + 6, FoundIndex, 3);
        if (!NT_SUCCESS(V->MFT->GetFileRecordInDirectory(Root, FoundName, &Existing)))
            break;
        delete Existing;
    }
    if (FoundIndex == 1000 ||
        !NT_SUCCESS(CreateLostDirectory(V, Root, FoundName, FILE_PERM_HIDDEN | FILE_PERM_SYSTEM, 0x90, &Found)))
    {
        delete Root;
        return;
    }
    delete Root;
    Event(NtfsCheckLostAndFound, Found->Header->MFTRecordNumber, FoundIndex);

    for (ULONG Index = 0; Index < LostCount; Index++)
    {
        ULONG Members = 0, First;
        PFileRecord Target = NULL;

        for (First = 0; First < LostCount && Lost[First].Parent != Lost[Index].Parent; First++)
        {
        }
        if (First < Index)
            continue;
        for (ULONG Other = Index; Other < LostCount; Other++)
        {
            if (Lost[Other].Parent == Lost[Index].Parent)
                Members++;
        }

        if (Members > 1)
        {
            PFileRecord Refreshed;

            FormatDecimal(GroupName + 3, Groups++, 4);
            if (!NT_SUCCESS(V->MFT->GetFileRecord(Found->Header->MFTRecordNumber, &Refreshed)))
                continue;
            if (!NT_SUCCESS(CreateLostDirectory(V, Refreshed, GroupName, 0, 0x80, &Target)))
            {
                delete Refreshed;
                continue;
            }
            delete Refreshed;
        }

        for (ULONG Other = Index; Other < LostCount; Other++)
        {
            PFileRecord File, Parent;
            PWCHAR NewName = NULL;
            NTSTATUS Status;

            if (Lost[Other].Parent != Lost[Index].Parent)
                continue;
            if (Members == 1)
            {
                for (ULONG Digit = 0; Digit < 8; Digit++)
                    NameBuffer[Digit] = Hex[(Singles >> (Digit * 4)) & 15];
                NameBuffer[8] = L'-';
                RtlCopyMemory(NameBuffer + 9, Lost[Other].Name,
                              min((ULONG)Lost[Other].NameLength, (ULONG)NTFS_MAX_FILE_NAME_LENGTH - 9) * sizeof(WCHAR));
                NameBuffer[9 + min((ULONG)Lost[Other].NameLength, (ULONG)NTFS_MAX_FILE_NAME_LENGTH - 9)] = 0;
                NewName = NameBuffer;
                Singles++;
            }

            if (!NT_SUCCESS(V->MFT->GetFileRecord((ULONG)Lost[Other].Number, &File)))
                continue;
            if (!NT_SUCCESS(V->MFT->GetFileRecord(Target ? Target->Header->MFTRecordNumber
                                                         : Found->Header->MFTRecordNumber,
                                                  &Parent)))
            {
                delete File;
                continue;
            }
            Status = V->MFT->ReconnectFile(File, Lost[Other].Parent, Parent, NewName);
            delete Parent;
            delete File;
            if (NT_SUCCESS(Status))
            {
                Out->Errors--;
                Out->RecoveredFiles++;
                Event(NtfsCheckFileRecovered, Lost[Other].Number,
                      Target ? Target->Header->MFTRecordNumber : Found->Header->MFTRecordNumber);
            }
        }
        delete Target;
    }

    delete Found;
    V->FlushVolume();
}

NTSTATUS
VolumeCheck::RepairRange(_In_ BOOLEAN Claim, _In_ ULONGLONG Lcn, _In_ ULONGLONG Count)
{
    DataRun Run;
    NTSTATUS Status;

    Run.NextRun = NULL;
    Run.LCN = Lcn;
    Run.Length = Count;
    Run.IsSparse = FALSE;
    Status = Claim ? V->ClaimClusters(&Run) : V->ReleaseClusters(&Run);
    if (NT_SUCCESS(Status))
    {
        if (Claim)
        {
            Out->ClaimedClusters += Count;
            Event(NtfsCheckUsedClustersClaimed, Lcn, Count);
        }
        else
        {
            Out->FreedClusters += Count;
            Event(NtfsCheckLostClustersFreed, Lcn, Count);
        }
    }
    return Status;
}

BOOLEAN
VolumeCheck::AddRange(_Inout_ RangeList* List, _In_ ULONGLONG Lcn)
{
    if (List->Count && List->Ranges[List->Count - 1].Lcn + List->Ranges[List->Count - 1].Length == Lcn)
    {
        List->Ranges[List->Count - 1].Length++;
        return TRUE;
    }
    if (List->Count == List->Capacity)
    {
        ULONG Capacity = List->Capacity ? List->Capacity * 2 : 64;
        ClusterRange* Grown = new(PagedPool, TAG_NTFS) ClusterRange[Capacity];

        if (!Grown)
            return FALSE;
        if (List->Ranges)
            RtlCopyMemory(Grown, List->Ranges, List->Count * sizeof(ClusterRange));
        delete[] List->Ranges;
        List->Ranges = Grown;
        List->Capacity = Capacity;
    }
    List->Ranges[List->Count].Lcn = Lcn;
    List->Ranges[List->Count].Length = 1;
    List->Count++;
    return TRUE;
}

NTSTATUS
VolumeCheck::CheckBitmap(_In_ BOOLEAN Repair)
{
    PUCHAR Chunk;
    ULONGLONG Lcn = 0;
    RangeList Lost = {}, Missing = {};
    NTSTATUS Status = STATUS_SUCCESS;

    Chunk = new(PagedPool, TAG_NTFS) UCHAR[CHECK_BITMAP_CHUNK];
    if (!Chunk)
        return STATUS_INSUFFICIENT_RESOURCES;

    while (NT_SUCCESS(Status) && Lcn < ClusterCount)
    {
        ULONGLONG Returned, Size;
        ULONG Length = CHECK_BITMAP_CHUNK;

        Status = V->ReadBitmap(Lcn, &Returned, &Size, Chunk, &Length);
        if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
            break;
        Status = STATUS_SUCCESS;
        if (Returned != Lcn || Length == 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }

        for (ULONG Bit = 0; Bit < Length * 8 && Lcn < ClusterCount && NT_SUCCESS(Status); Bit++, Lcn++)
        {
            BOOLEAN Marked = TestBit(Chunk, Bit);
            BOOLEAN Used = TestBit(UsedMap, Lcn);

            if (Marked != Used && !AddRange(Used ? &Missing : &Lost, Lcn))
                Status = STATUS_INSUFFICIENT_RESOURCES;
            if (Marked && !Used)
                Out->LostClusters++;
        }
    }
    delete[] Chunk;

    if (NT_SUCCESS(Status))
    {
        BOOLEAN Fix = Repair && StructuralErrors == 0 && !V->IsReadOnly;

        for (ULONG Index = 0; Index < Missing.Count; Index++)
            Event(NtfsCheckUsedClustersFree, Missing.Ranges[Index].Lcn, Missing.Ranges[Index].Length);
        if (Out->LostClusters)
            Event(NtfsCheckLostClusters, Out->LostClusters, Lost.Count);

        for (ULONG Index = 0; Fix && NT_SUCCESS(Status) && Index < Missing.Count; Index++)
            Status = RepairRange(TRUE, Missing.Ranges[Index].Lcn, Missing.Ranges[Index].Length);
        for (ULONG Index = 0; Fix && NT_SUCCESS(Status) && Index < Lost.Count; Index++)
            Status = RepairRange(FALSE, Lost.Ranges[Index].Lcn, Lost.Ranges[Index].Length);
        if (Fix && NT_SUCCESS(Status))
            Out->Errors -= Missing.Count + (Out->LostClusters ? 1 : 0);
    }

    delete[] Lost.Ranges;
    delete[] Missing.Ranges;
    return Status;
}

BOOLEAN
VolumeCheck::RecordHasName(_In_ ULONGLONG Ordinal, _In_ const FileNameEx* Key, _In_ ULONG KeyLength)
{
    PUCHAR Record = new(PagedPool, TAG_NTFS) UCHAR[RecordSize];
    PFileRecordHeader Header = reinterpret_cast<PFileRecordHeader>(Record);
    ULONGLONG DiskOffset;
    ULONG Offset, NameBytes = FIELD_OFFSET(FileNameEx, Name) + Key->NameLength * sizeof(WCHAR);
    BOOLEAN Found = FALSE;

    if (!Record)
        return FALSE;
    if (NameBytes > KeyLength ||
        !NT_SUCCESS(V->MFT->GetFileRecordDiskOffset(Ordinal, &DiskOffset)) ||
        !NT_SUCCESS(V->ReadVolume(DiskOffset, RecordSize, Record)) ||
        RtlCompareMemory(Header->Header.TypeID, "FILE", 4) != 4 ||
        !NT_SUCCESS(NtfsApplyFixup(&Header->Header, RecordSize, V->BytesPerSector)) ||
        Header->ActualSize > RecordSize)
    {
        delete[] Record;
        return FALSE;
    }

    for (Offset = Header->AttributeOffset; !Found && Offset + FIELD_OFFSET(Attribute, Resident) + 8 <= Header->ActualSize;)
    {
        PAttribute Attr = reinterpret_cast<PAttribute>(Record + Offset);

        if (Attr->AttributeType == TypeAttributeEndMarker || Attr->Length == 0 ||
            Attr->Length > Header->ActualSize - Offset)
        {
            break;
        }
        if (Attr->AttributeType == TypeFileName && !Attr->IsNonResident &&
            Attr->Resident.DataLength >= NameBytes &&
            (ULONG)Attr->Resident.DataOffset + NameBytes <= Attr->Length)
        {
            const FileNameEx* Name =
                reinterpret_cast<const FileNameEx*>(reinterpret_cast<PUCHAR>(Attr) + Attr->Resident.DataOffset);

            Found = Name->ParentFileReference == Key->ParentFileReference &&
                    Name->NameLength == Key->NameLength &&
                    Name->NameType == Key->NameType &&
                    RtlCompareMemory(Name->Name, Key->Name, Key->NameLength * sizeof(WCHAR)) ==
                        Key->NameLength * sizeof(WCHAR);
        }
        Offset += Attr->Length;
    }

    delete[] Record;
    return Found;
}

NTSTATUS
VolumeCheck::WriteStreamRange(_In_ PDataRun Runs, _In_ ULONGLONG Offset, _In_ ULONG Length, _In_ PUCHAR Data)
{
    ULONGLONG RunStart = 0;
    NTSTATUS Status;

    for (PDataRun Run = Runs; Run && Length; Run = Run->NextRun)
    {
        ULONGLONG RunBytes = Run->Length * ClusterSize;

        if (Offset < RunStart + RunBytes)
        {
            ULONGLONG Within = Offset - RunStart;
            ULONG Chunk = (ULONG)min((ULONGLONG)Length, RunBytes - Within);

            if (Run->IsSparse)
                return STATUS_FILE_CORRUPT_ERROR;
            Status = V->WriteVolume(Run->LCN * ClusterSize + Within, Chunk, Data);
            if (!NT_SUCCESS(Status))
                return Status;
            Offset += Chunk;
            Data += Chunk;
            Length -= Chunk;
        }
        RunStart += RunBytes;
    }
    return Length ? STATUS_FILE_CORRUPT_ERROR : STATUS_SUCCESS;
}

ULONG
VolumeCheck::CheckIndexEntries(_In_ ULONGLONG Directory,
                               _In_ PIndexNodeHeader Node,
                               _In_ ULONG Available,
                               _In_ ULONGLONG Vcn,
                               _In_ BOOLEAN AllowFix)
{
    PUCHAR Base = reinterpret_cast<PUCHAR>(Node);
    ULONG Offset = Node->IndexOffset;
    ULONG End = Node->TotalIndexSize;
    ULONG Fixed = 0;

    if (End > Available || Offset >= End || (Offset & 7) != 0)
    {
        Event(NtfsCheckIndexCorrupt, Directory, Vcn);
        return 0;
    }

    while (Offset + FIELD_OFFSET(IndexEntry, IndexStream) <= End)
    {
        PIndexEntry Entry = reinterpret_cast<PIndexEntry>(Base + Offset);
        const FileNameEx* Key = reinterpret_cast<const FileNameEx*>(Entry->IndexStream);
        ULONGLONG Target, Ordinal;

        if (Entry->EntryLength < FIELD_OFFSET(IndexEntry, IndexStream) || (Entry->EntryLength & 7) != 0 ||
            Entry->EntryLength > End - Offset ||
            Entry->StreamLength > Entry->EntryLength - FIELD_OFFSET(IndexEntry, IndexStream))
        {
            Event(NtfsCheckIndexCorrupt, Directory, Vcn);
            return Fixed;
        }
        if (Entry->Flags & INDEX_ENTRY_END)
            return Fixed;
        if (Entry->StreamLength < FIELD_OFFSET(FileNameEx, Name) ||
            Entry->StreamLength < FIELD_OFFSET(FileNameEx, Name) + Key->NameLength * sizeof(WCHAR))
        {
            Event(NtfsCheckIndexCorrupt, Directory, Vcn);
            return Fixed;
        }

        Out->IndexEntries++;
        Target = Entry->Data.Directory.IndexedFile;
        Ordinal = Target & 0x0000FFFFFFFFFFFFULL;
        if (Ordinal < RecordCount && TestBit(InUseMap, Ordinal) &&
            Sequence[Ordinal] == (USHORT)(Target >> 48) &&
            (Key->ParentFileReference & 0x0000FFFFFFFFFFFFULL) == Directory)
        {
            if (HasNameKey(Ordinal, HashName(Key)))
            {
                Names[Ordinal].Found++;
                Names[Ordinal].FoundHash += HashName(Key);
            }
            else
            {
                Event(NtfsCheckIndexNameMissing, Directory, Target);
                if (RepairIndexes)
                    RememberDangling(Directory, Target, Key);
            }
        }
        else if (Ordinal < RecordCount && TestBit(InUseMap, Ordinal) &&
                 (Key->ParentFileReference & 0x0000FFFFFFFFFFFFULL) == Directory &&
                 RecordHasName(Ordinal, Key, Entry->StreamLength))
        {
            Names[Ordinal].Found++;
            Names[Ordinal].FoundHash += HashName(Key);
            Event(NtfsCheckIndexStaleEntry, Directory, Target);
            if (AllowFix && RepairIndexes)
            {
                Entry->Data.Directory.IndexedFile = Ordinal | ((ULONGLONG)Sequence[Ordinal] << 48);
                Fixed++;
            }
        }
        else
        {
            Event(NtfsCheckIndexBadEntry, Directory, Target);
            if (RepairIndexes)
                RememberDangling(Directory, Target, Key);
        }
        Offset += Entry->EntryLength;
    }

    Event(NtfsCheckIndexCorrupt, Directory, Vcn);
    return Fixed;
}

void
VolumeCheck::RememberDangling(_In_ ULONGLONG Directory, _In_ ULONGLONG Target, _In_ const FileNameEx* Key)
{
    if (DanglingCount == DanglingCapacity)
    {
        ULONG Capacity = DanglingCapacity ? DanglingCapacity * 2 : 16;
        DanglingEntry* Grown = new(PagedPool, TAG_NTFS) DanglingEntry[Capacity];

        if (!Grown)
            return;
        if (Dangling)
            RtlCopyMemory(Grown, Dangling, DanglingCount * sizeof(DanglingEntry));
        delete[] Dangling;
        Dangling = Grown;
        DanglingCapacity = Capacity;
    }

    Dangling[DanglingCount].Directory = Directory;
    Dangling[DanglingCount].Target = Target;
    Dangling[DanglingCount].NameLength = Key->NameLength;
    RtlCopyMemory(Dangling[DanglingCount].Name, Key->Name, Key->NameLength * sizeof(WCHAR));
    DanglingCount++;
}

void
VolumeCheck::RemoveDangling()
{
    for (ULONG Index = 0; Index < DanglingCount; Index++)
    {
        DanglingEntry* Entry = &Dangling[Index];
        UNICODE_STRING Name;
        PFileRecord File;
        NTSTATUS Status;

        if (!NT_SUCCESS(V->MFT->GetFileRecord((ULONG)Entry->Directory, &File)))
            continue;

        Name.Buffer = Entry->Name;
        Name.Length = Name.MaximumLength = (USHORT)(Entry->NameLength * sizeof(WCHAR));
        {
            Directory Index(V);

            Status = Index.RemoveFileFromDirectory(File, Entry->Target, &Name);
        }
        delete File;

        if (NT_SUCCESS(Status))
        {
            Out->Errors--;
            Out->RemovedIndexEntries++;
            StructuralErrors--;
            Event(NtfsCheckIndexEntryRemoved, Entry->Directory, Entry->Target);
        }
    }
    if (DanglingCount)
        V->FlushVolume();
}

void
VolumeCheck::CheckDirectory(_In_ ULONGLONG Directory)
{
    PFileRecord File;
    PAttribute RootAttr, AllocationAttr, BitmapAttr;
    PIndexRootEx Root;
    PUCHAR Allocation = NULL, Bitmap = NULL;
    ULONG AllocationLength = 0, BitmapLength = 0;
    PDataRun Runs = NULL;
    ULONG Fixed;
    BOOLEAN RootInBase;
    NTSTATUS Status;

    Out->Directories++;
    Status = V->MFT->GetFileRecord((ULONG)Directory, &File);
    if (!NT_SUCCESS(Status))
    {
        Event(NtfsCheckIndexCorrupt, Directory, 0);
        return;
    }

    RootAttr = File->GetAttribute(TypeIndexRoot, const_cast<PWSTR>(L"$I30"));
    if (!RootAttr || RootAttr->IsNonResident ||
        RootAttr->Resident.DataLength < sizeof(IndexRootEx))
    {
        Event(NtfsCheckIndexCorrupt, Directory, 0);
        delete File;
        return;
    }
    Root = reinterpret_cast<PIndexRootEx>(reinterpret_cast<PUCHAR>(RootAttr) + RootAttr->Resident.DataOffset);
    RootInBase = reinterpret_cast<PUCHAR>(RootAttr) >= File->Data &&
                 reinterpret_cast<PUCHAR>(RootAttr) + RootAttr->Length <= File->Data + RecordSize;
    Fixed = CheckIndexEntries(Directory,
                              &Root->Header,
                              RootAttr->Resident.DataLength - FIELD_OFFSET(IndexRootEx, Header),
                              0,
                              RootInBase);
    if (Fixed && NT_SUCCESS(V->MFT->WriteFileRecordToMFT(File)))
    {
        Out->Errors -= Fixed;
        Out->FixedIndexEntries += Fixed;
        Event(NtfsCheckIndexEntryFixed, Directory, Fixed);
    }

    AllocationAttr = File->GetAttribute(TypeIndexAllocation, const_cast<PWSTR>(L"$I30"));
    BitmapAttr = File->GetAttribute(TypeBitmap, const_cast<PWSTR>(L"$I30"));
    if (AllocationAttr)
    {
        ULONG BlockSize = Root->BytesPerIndexRec;

        if (!BitmapAttr || BlockSize < sizeof(IndexBuffer) || (BlockSize % V->BytesPerSector) != 0 ||
            !NT_SUCCESS(File->ReadAttributeAlloc(AllocationAttr, &Allocation, &AllocationLength)) ||
            !NT_SUCCESS(File->ReadAttributeAlloc(BitmapAttr, &Bitmap, &BitmapLength)))
        {
            Event(NtfsCheckIndexCorrupt, Directory, 0);
        }
        else
        {
            ULONG Blocks = AllocationLength / BlockSize;
            ULONG VcnSize = BlockSize >= ClusterSize ? ClusterSize : 512;
            PUCHAR Scratch = NULL;

            if (RepairIndexes)
            {
                Runs = File->FindNonResidentData(AllocationAttr);
                Scratch = new(PagedPool, TAG_NTFS) UCHAR[BlockSize];
            }

            for (ULONG Block = 0; Block < Blocks && Block < BitmapLength * 8; Block++)
            {
                PIndexBuffer Buffer = reinterpret_cast<PIndexBuffer>(Allocation + Block * BlockSize);
                ULONGLONG Vcn = (ULONGLONG)Block * BlockSize / VcnSize;

                if (!TestBit(Bitmap, Block))
                    continue;
                if (RtlCompareMemory(Buffer->RecordHeader.TypeID, "INDX", 4) != 4 ||
                    !NT_SUCCESS(NtfsApplyFixup(&Buffer->RecordHeader, BlockSize, V->BytesPerSector)) ||
                    Buffer->VCN != Vcn)
                {
                    Event(NtfsCheckIndexCorrupt, Directory, Vcn);
                    continue;
                }
                Fixed = CheckIndexEntries(Directory,
                                          &Buffer->IndexHeader,
                                          BlockSize - FIELD_OFFSET(IndexBuffer, IndexHeader),
                                          Vcn,
                                          Runs && Scratch);
                if (!Fixed)
                    continue;

                RtlCopyMemory(Scratch, Buffer, BlockSize);
                if (NT_SUCCESS(NtfsCommitFixup(reinterpret_cast<PNTFSRecordHeader>(Scratch),
                                               BlockSize,
                                               V->BytesPerSector)) &&
                    NT_SUCCESS(WriteStreamRange(Runs, (ULONGLONG)Block * BlockSize, BlockSize, Scratch)))
                {
                    Out->Errors -= Fixed;
                    Out->FixedIndexEntries += Fixed;
                    Event(NtfsCheckIndexEntryFixed, Directory, Fixed);
                }
            }
            delete[] Scratch;
        }
    }
    else if (Root->Header.Flags & 1)
    {
        Event(NtfsCheckIndexCorrupt, Directory, 0);
    }

    if (Runs)
        FreeDataRun(Runs);
    if (RepairIndexes)
        V->FlushVolume();
    delete[] Allocation;
    delete[] Bitmap;
    delete File;
}

NTSTATUS
VolumeCheck::Run(_In_ BOOLEAN Repair)
{
    NtfsVolumeInformation Information;
    NTSTATUS Status;

    RtlZeroMemory(Out, sizeof(*Out));
    Status = V->QueryInformation(&Information);
    if (!NT_SUCCESS(Status))
        return Status;

    ClusterSize = Information.BytesPerCluster;
    RecordSize = Information.BytesPerFileRecordSegment;
    ClusterCount = V->ClustersInVolume;
    if (!ClusterSize || !RecordSize || RecordSize % V->BytesPerSector)
        return STATUS_FILE_CORRUPT_ERROR;
    RecordCount = Information.MftValidDataLength / RecordSize;
    if (RecordCount <= CHECK_FIRST_USER_RECORD || RecordCount > MAXULONG)
        return STATUS_FILE_CORRUPT_ERROR;

    UsedMap = AllocateMap(ClusterCount);
    DirectoryMap = AllocateMap(RecordCount);
    InUseMap = AllocateMap(RecordCount);
    Sequence = new(PagedPool, TAG_NTFS) USHORT[(ULONG)RecordCount];
    Names = new(PagedPool, TAG_NTFS) NameTally[(ULONG)RecordCount];
    Records = new(PagedPool, TAG_NTFS) UCHAR[CHECK_RECORD_CHUNK * RecordSize];
    ExtensionOwner = new(PagedPool, TAG_NTFS) ULONG[(ULONG)RecordCount];
    AttrListMap = AllocateMap(RecordCount);
    if (!UsedMap || !DirectoryMap || !InUseMap || !Sequence || !Names || !Records || !ExtensionOwner || !AttrListMap)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(ExtensionOwner, (ULONG)RecordCount * sizeof(ULONG));
    RtlZeroMemory(Sequence, (ULONG)RecordCount * sizeof(USHORT));
    RtlZeroMemory(Names, (ULONG)RecordCount * sizeof(NameTally));

    Status = LoadMftBitmap();
    if (!NT_SUCCESS(Status))
        return Status;

    RepairRecords = Repair && !V->IsReadOnly;
    for (ULONGLONG First = 0; First < RecordCount; First += CHECK_RECORD_CHUNK)
    {
        ULONG Count = (ULONG)min((ULONGLONG)CHECK_RECORD_CHUNK, RecordCount - First);

        Status = ReadRecords(First, Count);
        if (!NT_SUCCESS(Status))
            return Status;
        for (ULONG Index = 0; Index < Count; Index++)
            CheckRecord(First + Index, Records + Index * RecordSize);
    }

    if (RepairRecords)
        RepairRecordsPass();

    SortNameKeys();
    CheckAttributeLists();

    RepairIndexes = Repair && StructuralErrors == 0 && !V->IsReadOnly;
    for (ULONGLONG Number = 0; Number < RecordCount; Number++)
    {
        if (TestBit(DirectoryMap, Number) && TestBit(InUseMap, Number))
            CheckDirectory(Number);
    }

    for (ULONGLONG Number = 0; Number < RecordCount; Number++)
    {
        if (TestBit(InUseMap, Number) &&
            (Names[Number].Expected != Names[Number].Found ||
             Names[Number].ExpectedHash != Names[Number].FoundHash))
        {
            Event(NtfsCheckNameNotIndexed, Number, Names[Number].Expected - Names[Number].Found);
            if (RepairIndexes && Names[Number].Expected > Names[Number].Found)
                RememberOrphan(Number);
        }
    }

    Status = CheckBitmap(Repair);
    if (NT_SUCCESS(Status) && RepairIndexes)
    {
        RemoveDangling();
        RelinkOrphans();
        RecoverLostFiles();
    }
    if (NT_SUCCESS(Status) && RepairRecords && CrossLinkCount && StructuralErrors == CrossLinkCount)
        ResolveCrossLinks();
    return Status;
}

}

extern "C"
NTSTATUS
NtfsVolumeCheck(
    _In_ PNtfsVolume DiskVolume,
    _In_ BOOLEAN Repair,
    _In_opt_ PNtfsCheckCallback Callback,
    _In_opt_ void* Context,
    _Out_ PNtfsCheckResult Result)
{
    NtfsCheckResult Pass;
    NTSTATUS Status;

    if (!DiskVolume || !Result)
        return STATUS_INVALID_PARAMETER;

    {
        VolumeCheck Check(reinterpret_cast<PVolume>(DiskVolume), Callback, Context, Result);

        Status = Check.Run(Repair);
    }
    if (!NT_SUCCESS(Status) || !Repair ||
        !(Result->RemovedIndexEntries || Result->RelinkedNames || Result->FreedRecords ||
          Result->ResolvedCrossLinks || Result->RecoveredFiles || Result->RepairedAttributeLists ||
          Result->RemovedNames))
    {
        return Status;
    }

    {
        VolumeCheck Check(reinterpret_cast<PVolume>(DiskVolume), Callback, Context, &Pass);

        Status = Check.Run(Repair);
    }
    if (NT_SUCCESS(Status))
    {
        Pass.FreedClusters += Result->FreedClusters;
        Pass.ClaimedClusters += Result->ClaimedClusters;
        Pass.FixedIndexEntries += Result->FixedIndexEntries;
        Pass.RemovedIndexEntries += Result->RemovedIndexEntries;
        Pass.RelinkedNames += Result->RelinkedNames;
        Pass.FreedRecords += Result->FreedRecords;
        Pass.ResolvedCrossLinks += Result->ResolvedCrossLinks;
        Pass.RecoveredFiles += Result->RecoveredFiles;
        Pass.RepairedAttributeLists += Result->RepairedAttributeLists;
        Pass.RemovedNames += Result->RemovedNames;
        *Result = Pass;
    }
    return Status;
}
