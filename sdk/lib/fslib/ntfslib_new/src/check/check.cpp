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
        if (Kind != NtfsCheckLostClustersFreed && Kind != NtfsCheckUsedClustersClaimed)
            Out->Errors++;
        if (Kind != NtfsCheckLostClustersFreed && Kind != NtfsCheckUsedClustersClaimed &&
            Kind != NtfsCheckLostClusters && Kind != NtfsCheckUsedClustersFree)
        {
            StructuralErrors++;
        }
        if (Report)
            Report(ReportContext, Kind, Arg1, Arg2);
    }

    NTSTATUS LoadMftBitmap();
    NTSTATUS ReadRecords(_In_ ULONGLONG First, _In_ ULONG Count);
    void CheckRecord(_In_ ULONGLONG Number, _Inout_ PUCHAR Record);
    void CheckAttributes(_In_ ULONGLONG Number, _In_ ULONGLONG Owner, _In_ PUCHAR Record);
    void CheckRunList(_In_ ULONGLONG Number, _In_ PAttribute Attr);
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
    void CheckIndexEntries(_In_ ULONGLONG Directory,
                           _In_ PIndexNodeHeader Node,
                           _In_ ULONG Available,
                           _In_ ULONGLONG Vcn);
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
VolumeCheck::CheckRunList(_In_ ULONGLONG Number, _In_ PAttribute Attr)
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
                        Event(NtfsCheckCrossLinked, Number, Cluster);
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

        if (Attr->IsNonResident)
        {
            if (Attr->Length < FIELD_OFFSET(Attribute, NonResident.CompressedDataSize))
            {
                Event(NtfsCheckRecordCorrupt, Number, Offset);
                return;
            }
            CheckRunList(Number, Attr);
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

    if (RtlCompareMemory(Header->Header.TypeID, "FILE", 4) != 4 ||
        !NT_SUCCESS(NtfsApplyFixup(&Header->Header, RecordSize, V->BytesPerSector)))
    {
        if (Allocated && Number >= CHECK_FIRST_USER_RECORD)
            Event(NtfsCheckRecordCorrupt, Number, 0);
        return;
    }

    InUse = (Header->Flags & FR_IN_USE) != 0;
    if (InUse != Allocated && (InUse || Number >= CHECK_FIRST_USER_RECORD))
        Event(NtfsCheckRecordBitmap, Number, InUse);
    if (!InUse)
        return;

    if (Header->AllocatedSize != RecordSize || Header->ActualSize > RecordSize ||
        Header->AttributeOffset < sizeof(NTFSRecordHeader) || (Header->AttributeOffset & 7) != 0 ||
        Header->AttributeOffset >= Header->ActualSize ||
        (Header->Header.UpdateSequenceOffset >= FIELD_OFFSET(FileRecordHeader, MFTRecordNumber) + sizeof(UINT32) &&
         Header->MFTRecordNumber != (UINT32)Number))
    {
        Event(NtfsCheckRecordCorrupt, Number, 0);
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

void
VolumeCheck::CheckIndexEntries(_In_ ULONGLONG Directory,
                               _In_ PIndexNodeHeader Node,
                               _In_ ULONG Available,
                               _In_ ULONGLONG Vcn)
{
    PUCHAR Base = reinterpret_cast<PUCHAR>(Node);
    ULONG Offset = Node->IndexOffset;
    ULONG End = Node->TotalIndexSize;

    if (End > Available || Offset >= End || (Offset & 7) != 0)
    {
        Event(NtfsCheckIndexCorrupt, Directory, Vcn);
        return;
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
            return;
        }
        if (Entry->Flags & INDEX_ENTRY_END)
            return;
        if (Entry->StreamLength < FIELD_OFFSET(FileNameEx, Name) ||
            Entry->StreamLength < FIELD_OFFSET(FileNameEx, Name) + Key->NameLength * sizeof(WCHAR))
        {
            Event(NtfsCheckIndexCorrupt, Directory, Vcn);
            return;
        }

        Out->IndexEntries++;
        Target = Entry->Data.Directory.IndexedFile;
        Ordinal = Target & 0x0000FFFFFFFFFFFFULL;
        if (Ordinal >= RecordCount || !TestBit(InUseMap, Ordinal) ||
            Sequence[Ordinal] != (USHORT)(Target >> 48) ||
            (Key->ParentFileReference & 0x0000FFFFFFFFFFFFULL) != Directory)
        {
            Event(NtfsCheckIndexBadEntry, Directory, Target);
        }
        else
        {
            Names[Ordinal].Found++;
            Names[Ordinal].FoundHash += HashName(Key);
        }
        Offset += Entry->EntryLength;
    }

    Event(NtfsCheckIndexCorrupt, Directory, Vcn);
}

void
VolumeCheck::CheckDirectory(_In_ ULONGLONG Directory)
{
    PFileRecord File;
    PAttribute RootAttr, AllocationAttr, BitmapAttr;
    PIndexRootEx Root;
    PUCHAR Allocation = NULL, Bitmap = NULL;
    ULONG AllocationLength = 0, BitmapLength = 0;
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
    CheckIndexEntries(Directory, &Root->Header, RootAttr->Resident.DataLength - FIELD_OFFSET(IndexRootEx, Header), 0);

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
                CheckIndexEntries(Directory,
                                  &Buffer->IndexHeader,
                                  BlockSize - FIELD_OFFSET(IndexBuffer, IndexHeader),
                                  Vcn);
            }
        }
    }
    else if (Root->Header.Flags & 1)
    {
        Event(NtfsCheckIndexCorrupt, Directory, 0);
    }

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
    if (!UsedMap || !DirectoryMap || !InUseMap || !Sequence || !Names || !Records)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Sequence, (ULONG)RecordCount * sizeof(USHORT));
    RtlZeroMemory(Names, (ULONG)RecordCount * sizeof(NameTally));

    Status = LoadMftBitmap();
    if (!NT_SUCCESS(Status))
        return Status;

    for (ULONGLONG First = 0; First < RecordCount; First += CHECK_RECORD_CHUNK)
    {
        ULONG Count = (ULONG)min((ULONGLONG)CHECK_RECORD_CHUNK, RecordCount - First);

        Status = ReadRecords(First, Count);
        if (!NT_SUCCESS(Status))
            return Status;
        for (ULONG Index = 0; Index < Count; Index++)
            CheckRecord(First + Index, Records + Index * RecordSize);
    }

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
        }
    }

    return CheckBitmap(Repair);
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
    VolumeCheck Check(reinterpret_cast<PVolume>(DiskVolume), Callback, Context, Result);

    if (!DiskVolume || !Result)
        return STATUS_INVALID_PARAMETER;
    return Check.Run(Repair);
}
