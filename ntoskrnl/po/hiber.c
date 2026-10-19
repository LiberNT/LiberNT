/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hibernation file and image writer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/drivers/dumpstor.h>
#include <reactos/hiberfil.h>
#define NDEBUG
#include <debug.h>

#define POP_HIBER_TAG               'bHoP'
#define POP_HIBER_STAGING_SIZE      (1024 * 1024)
#define POP_HIBER_CHUNK_SIZE        (LNT_HIBER_CHUNK_PAGES * PAGE_SIZE)
#define POP_HIBER_LZ_CAPACITY       (POP_HIBER_CHUNK_SIZE + POP_HIBER_CHUNK_SIZE / 255 + 64)
#define POP_HIBER_MAX_RANGES        65536
#define POP_HIBER_MAX_HAL_RANGES    64
#define POP_HIBER_EXTENT_BUFFER     (64 * 1024)
#define POP_HIBER_LZ_HASH_BITS      12
#define POP_HIBER_DEFAULT_PERCENT   40

typedef struct _POP_HIBER_WAKE_CONTEXT
{
    LNT_HIBER_RESUME_TARGET Target;
} POP_HIBER_WAKE_CONTEXT, *PPOP_HIBER_WAKE_CONTEXT;

typedef struct _POP_HIBER_STATE
{
    KMUTEX Lock;
    HANDLE FileHandle;
    PFILE_OBJECT FileObject;
    PRETRIEVAL_POINTERS_BUFFER Extents;
    ROS_STORAGE_DUMP_INTERFACE Storage;
    LARGE_INTEGER PartitionOffset;
    ULONG64 Capacity;
    ULONG ClusterSize;
    ULONG ExtentIndex;
    ULONG64 ExtentStartVcn;
    PUCHAR Staging;
    PHYSICAL_ADDRESS StagingPhysical;
    ULONG StagingUsed;
    ULONG64 StagingFileOffset;
    PUCHAR Chunk;
    PUCHAR LzOutput;
    PUSHORT LzTable;
    PLNT_HIBER_RANGE Ranges;
    ULONG64 RangeCount;
    ULONG64 TargetPages;
    ULONG64 DataPages;
    ULONG64 HighestPage;
    ULONG64 RecordLength;
    LNT_HIBER_RANGE HalRanges[POP_HIBER_MAX_HAL_RANGES];
    ULONG HalRangeCount;
    LNT_HIBER_RUNTIME_RANGE RuntimeRanges[LNT_HIBER_MAX_RUNTIME_RANGES];
    ULONG RuntimeRangeCount;
    BOOLEAN Ready;
    BOOLEAN Prepared;
    WORK_QUEUE_ITEM WorkItem;
    POP_HIBER_WAKE_CONTEXT Context;
} POP_HIBER_STATE;

static POP_HIBER_STATE PopHiber;
static volatile LONG PopHiberStartState;

#define POP_HIBER_START_CAPTURED    0x1
#define POP_HIBER_START_PLATFORM    0x2
#define POP_HIBER_START_QUEUED      0x4

static
BOOLEAN
PopHiberLzEmit(
    _Inout_ PUCHAR *Output,
    _In_ PUCHAR OutputEnd,
    _In_reads_bytes_(LiteralLength) const UCHAR *Literals,
    _In_ ULONG LiteralLength,
    _In_ ULONG Offset,
    _In_ ULONG MatchLength,
    _In_ BOOLEAN HasMatch)
{
    PUCHAR Op = *Output;
    ULONG Length;

    if ((ULONG_PTR)(OutputEnd - Op) < 1 + LiteralLength + LiteralLength / 255 + 1 + 2 + MatchLength / 255 + 1)
        return FALSE;

    *Op++ = (UCHAR)((min(LiteralLength, 15) << 4) | (HasMatch ? min(MatchLength, 15) : 0));
    if (LiteralLength >= 15)
    {
        for (Length = LiteralLength - 15; Length >= 255; Length -= 255)
            *Op++ = 255;
        *Op++ = (UCHAR)Length;
    }

    RtlCopyMemory(Op, Literals, LiteralLength);
    Op += LiteralLength;

    if (HasMatch)
    {
        *Op++ = (UCHAR)Offset;
        *Op++ = (UCHAR)(Offset >> 8);
        if (MatchLength >= 15)
        {
            for (Length = MatchLength - 15; Length >= 255; Length -= 255)
                *Op++ = 255;
            *Op++ = (UCHAR)Length;
        }
    }

    *Output = Op;
    return TRUE;
}

static
ULONG
PopHiberLzCompress(
    _In_reads_bytes_(Length) const UCHAR *Source,
    _In_ ULONG Length,
    _Out_writes_bytes_(Capacity) PUCHAR Destination,
    _In_ ULONG Capacity,
    _Inout_ PUSHORT Table)
{
    const UCHAR *Anchor = Source;
    const UCHAR *Ip = Source;
    const UCHAR *End = Source + Length;
    const UCHAR *Limit = (Length > 12) ? End - 12 : Source;
    PUCHAR Op = Destination;
    PUCHAR OpEnd = Destination + Capacity;

    RtlZeroMemory(Table, sizeof(USHORT) << POP_HIBER_LZ_HASH_BITS);

    while (Ip < Limit)
    {
        ULONG Sequence = *(UNALIGNED ULONG *)Ip;
        ULONG Hash = (Sequence * 2654435761U) >> (32 - POP_HIBER_LZ_HASH_BITS);
        const UCHAR *Ref = Source + Table[Hash];
        const UCHAR *MatchEnd;
        const UCHAR *RefEnd;

        Table[Hash] = (USHORT)(Ip - Source);

        if (Ref >= Ip || (ULONG_PTR)(Ip - Ref) > 0xFFFF || *(UNALIGNED ULONG *)Ref != Sequence)
        {
            Ip++;
            continue;
        }

        MatchEnd = Ip + 4;
        RefEnd = Ref + 4;
        while (MatchEnd < End - 5 && *MatchEnd == *RefEnd)
        {
            MatchEnd++;
            RefEnd++;
        }

        if (!PopHiberLzEmit(&Op, OpEnd, Anchor, (ULONG)(Ip - Anchor), (ULONG)(Ip - Ref),
                            (ULONG)(MatchEnd - Ip) - 4, TRUE))
        {
            return 0;
        }

        Ip = MatchEnd;
        Anchor = Ip;
    }

    if (!PopHiberLzEmit(&Op, OpEnd, Anchor, (ULONG)(End - Anchor), 0, 0, FALSE))
        return 0;

    return (ULONG)(Op - Destination);
}

static
UCHAR
PopHiberClassify(
    _In_ PFN_NUMBER Frame)
{
    ULONG Index;

    for (Index = 0; Index < PopHiber.HalRangeCount; Index++)
    {
        if (Frame >= PopHiber.HalRanges[Index].BasePage &&
            Frame < PopHiber.HalRanges[Index].BasePage + PopHiber.HalRanges[Index].PageCount)
        {
            return MM_HIBERNATE_SAVE;
        }
    }

    return MmQueryHibernationFrame(Frame);
}

static
NTSTATUS
PopHiberTranslate(
    _In_ ULONG64 FileOffset,
    _In_ ULONG Length,
    _Out_ PULONG64 DiskByteOffset,
    _Out_ PULONG ContiguousLength)
{
    PRETRIEVAL_POINTERS_BUFFER Extents = PopHiber.Extents;
    ULONG64 Vcn;
    ULONG64 RunStartVcn;
    ULONG64 RunEndVcn;
    ULONG64 OffsetInCluster;
    ULONG Index;

    if (FileOffset >= PopHiber.Capacity || (ULONG64)Length > PopHiber.Capacity - FileOffset)
        return STATUS_DISK_FULL;

    Vcn = FileOffset / PopHiber.ClusterSize;
    OffsetInCluster = FileOffset % PopHiber.ClusterSize;

    Index = PopHiber.ExtentIndex;
    RunStartVcn = PopHiber.ExtentStartVcn;
    if (Vcn < RunStartVcn)
    {
        Index = 0;
        RunStartVcn = Extents->StartingVcn.QuadPart;
    }

    for (; Index < Extents->ExtentCount; Index++)
    {
        RunEndVcn = Extents->Extents[Index].NextVcn.QuadPart;
        if (Vcn >= RunStartVcn && Vcn < RunEndVcn)
        {
            if (Extents->Extents[Index].Lcn.QuadPart < 0)
                return STATUS_FILE_CORRUPT_ERROR;

            PopHiber.ExtentIndex = Index;
            PopHiber.ExtentStartVcn = RunStartVcn;
            *DiskByteOffset = PopHiber.PartitionOffset.QuadPart +
                              (ULONG64)(Extents->Extents[Index].Lcn.QuadPart + (Vcn - RunStartVcn)) *
                              PopHiber.ClusterSize + OffsetInCluster;
            *ContiguousLength = (ULONG)min((ULONG64)Length,
                                           (RunEndVcn - Vcn) * PopHiber.ClusterSize - OffsetInCluster);
            return STATUS_SUCCESS;
        }

        RunStartVcn = RunEndVcn;
    }

    return STATUS_END_OF_FILE;
}

static
NTSTATUS
PopHiberWriteStaging(
    _In_ ULONG Length)
{
    PHYSICAL_ADDRESS Physical;
    ULONG64 DiskByteOffset;
    ULONG Contiguous;
    ULONG Transfer;
    ULONG Done = 0;
    NTSTATUS Status;

    while (Done < Length)
    {
        Status = PopHiberTranslate(PopHiber.StagingFileOffset + Done, Length - Done, &DiskByteOffset, &Contiguous);
        if (!NT_SUCCESS(Status))
            return Status;

        Transfer = min(Contiguous, PopHiber.Storage.MaximumTransferLength);
        Physical.QuadPart = PopHiber.StagingPhysical.QuadPart + Done;
        Status = PopHiber.Storage.WriteRoutine(PopHiber.Storage.Context, DiskByteOffset, Physical, Transfer);
        if (!NT_SUCCESS(Status))
            return Status;

        Done += Transfer;
    }

    PopHiber.StagingFileOffset += Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PopHiberAppend(
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    const UCHAR *Source = Data;
    ULONG Copy;
    NTSTATUS Status;

    while (Length != 0)
    {
        Copy = min(Length, POP_HIBER_STAGING_SIZE - PopHiber.StagingUsed);
        RtlCopyMemory(PopHiber.Staging + PopHiber.StagingUsed, Source, Copy);
        PopHiber.StagingUsed += Copy;
        PopHiber.RecordLength += Copy;
        Source += Copy;
        Length -= Copy;

        if (PopHiber.StagingUsed == POP_HIBER_STAGING_SIZE)
        {
            Status = PopHiberWriteStaging(POP_HIBER_STAGING_SIZE);
            if (!NT_SUCCESS(Status))
                return Status;
            PopHiber.StagingUsed = 0;
        }
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
PopHiberFlush(VOID)
{
    ULONG Padded = ALIGN_UP_BY(PopHiber.StagingUsed, PAGE_SIZE);
    NTSTATUS Status;

    if (Padded == 0)
        return STATUS_SUCCESS;

    RtlZeroMemory(PopHiber.Staging + PopHiber.StagingUsed, Padded - PopHiber.StagingUsed);
    Status = PopHiberWriteStaging(Padded);
    PopHiber.StagingUsed = 0;
    return Status;
}

static
NTSTATUS
PopHiberAddRange(
    _In_ PFN_NUMBER BasePage,
    _In_ PFN_NUMBER PageCount,
    _In_ UCHAR Class)
{
    if (PopHiber.RangeCount == POP_HIBER_MAX_RANGES)
        return STATUS_INSUFFICIENT_RESOURCES;

    PopHiber.Ranges[PopHiber.RangeCount].BasePage = BasePage;
    PopHiber.Ranges[PopHiber.RangeCount].PageCount = (ULONG)PageCount;
    PopHiber.Ranges[PopHiber.RangeCount].Class = (Class == MM_HIBERNATE_SAVE) ? LNT_HIBER_RANGE_DATA : LNT_HIBER_RANGE_ZERO;
    PopHiber.RangeCount++;
    PopHiber.TargetPages += PageCount;
    if (Class == MM_HIBERNATE_SAVE)
        PopHiber.DataPages += PageCount;
    if (BasePage + PageCount > PopHiber.HighestPage)
        PopHiber.HighestPage = BasePage + PageCount;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PopHiberBuildRanges(VOID)
{
    PFN_NUMBER Limit = MmHibernationFrameLimit();
    PFN_NUMBER Frame;
    PFN_NUMBER RunStart = 0;
    UCHAR RunClass = MM_HIBERNATE_SKIP;
    UCHAR Class;
    ULONG Index;
    NTSTATUS Status;

    for (Index = 0; Index < PopHiber.HalRangeCount; Index++)
    {
        if (PopHiber.HalRanges[Index].BasePage + PopHiber.HalRanges[Index].PageCount > Limit)
            Limit = (PFN_NUMBER)(PopHiber.HalRanges[Index].BasePage + PopHiber.HalRanges[Index].PageCount);
    }

    PopHiber.RangeCount = 0;
    PopHiber.TargetPages = 0;
    PopHiber.DataPages = 0;
    PopHiber.HighestPage = 0;

    for (Frame = 0; Frame < Limit; Frame++)
    {
        Class = PopHiberClassify(Frame);
        if (Class == RunClass && (Class == MM_HIBERNATE_SKIP || Frame - RunStart < MAXLONG))
            continue;

        if (RunClass != MM_HIBERNATE_SKIP)
        {
            Status = PopHiberAddRange(RunStart, Frame - RunStart, RunClass);
            if (!NT_SUCCESS(Status))
                return Status;
        }

        RunStart = Frame;
        RunClass = Class;
    }

    if (RunClass != MM_HIBERNATE_SKIP)
        return PopHiberAddRange(RunStart, Limit - RunStart, RunClass);

    return STATUS_SUCCESS;
}

static
BOOLEAN
PopHiberChunkIsZero(
    _In_reads_bytes_(Length) const VOID *Data,
    _In_ ULONG Length)
{
    const ULONG64 *Quad = Data;
    ULONG Index;

    for (Index = 0; Index < Length / sizeof(ULONG64); Index++)
    {
        if (Quad[Index] != 0)
            return FALSE;
    }

    return TRUE;
}

static
NTSTATUS
PopHiberEmitRecord(
    _In_ PFN_NUMBER TargetPage,
    _In_ ULONG PageCount,
    _In_ USHORT Type,
    _In_reads_bytes_opt_(DataLength) const VOID *Data,
    _In_ ULONG DataLength)
{
    static const UCHAR Padding[8] = { 0 };
    LNT_HIBER_RECORD Record;
    NTSTATUS Status;

    RtlZeroMemory(&Record, sizeof(Record));
    Record.TargetPage = TargetPage;
    Record.PageCount = PageCount;
    Record.Type = Type;
    Record.DataLength = DataLength;

    Status = PopHiberAppend(&Record, sizeof(Record));
    if (!NT_SUCCESS(Status) || DataLength == 0)
        return Status;

    Status = PopHiberAppend(Data, DataLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (DataLength & 7)
        Status = PopHiberAppend(Padding, 8 - (DataLength & 7));

    return Status;
}

static
NTSTATUS
PopHiberWriteRecords(VOID)
{
    PFN_NUMBER Frame;
    PFN_NUMBER End;
    ULONG64 Index;
    ULONG64 Written = 0;
    ULONG Count;
    ULONG Page;
    ULONG Compressed;
    ULONG Tenth = 0;
    PVOID Mapping;
    NTSTATUS Status;

    for (Index = 0; Index < PopHiber.RangeCount; Index++)
    {
        Frame = (PFN_NUMBER)PopHiber.Ranges[Index].BasePage;
        End = Frame + (PFN_NUMBER)PopHiber.Ranges[Index].PageCount;

        if (PopHiber.Ranges[Index].Class == LNT_HIBER_RANGE_ZERO)
        {
            Status = PopHiberEmitRecord(Frame, (ULONG)(End - Frame), LNT_HIBER_RECORD_ZERO, NULL, 0);
            if (!NT_SUCCESS(Status))
                return Status;
            continue;
        }

        while (Frame < End)
        {
            Count = (ULONG)min((PFN_NUMBER)LNT_HIBER_CHUNK_PAGES, End - Frame);

            for (Page = 0; Page < Count; Page++)
            {
                Mapping = MmMapHibernationFrame(Frame + Page);
                if (Mapping == NULL)
                    return STATUS_UNSUCCESSFUL;
                RtlCopyMemory(PopHiber.Chunk + Page * PAGE_SIZE, Mapping, PAGE_SIZE);
            }

            if (PopHiberChunkIsZero(PopHiber.Chunk, Count * PAGE_SIZE))
            {
                Status = PopHiberEmitRecord(Frame, Count, LNT_HIBER_RECORD_ZERO, NULL, 0);
            }
            else
            {
                Compressed = PopHiberLzCompress(PopHiber.Chunk, Count * PAGE_SIZE,
                                                PopHiber.LzOutput, POP_HIBER_LZ_CAPACITY, PopHiber.LzTable);
                if (Compressed != 0 && Compressed < Count * PAGE_SIZE)
                    Status = PopHiberEmitRecord(Frame, Count, LNT_HIBER_RECORD_LZ, PopHiber.LzOutput, Compressed);
                else
                    Status = PopHiberEmitRecord(Frame, Count, LNT_HIBER_RECORD_RAW, PopHiber.Chunk, Count * PAGE_SIZE);
            }

            if (!NT_SUCCESS(Status))
                return Status;

            Frame += Count;
            Written += Count;
            if (Written * 10 / PopHiber.DataPages > Tenth)
            {
                Tenth = (ULONG)(Written * 10 / PopHiber.DataPages);
                DPRINT1("Writing the hibernation image: %lu%%\n", Tenth * 10);
            }
        }
    }

    return PopHiberEmitRecord(0, 0, LNT_HIBER_RECORD_END, NULL, 0);
}

NTSTATUS
NTAPI
PopHiberSystemHandler(
    _In_ PVOID SystemContext)
{
    PPOP_HIBER_WAKE_CONTEXT Context = SystemContext;
    PLNT_HIBER_HEADER Header;
    ULONG64 RecordOffset;
    NTSTATUS Status;

    if (!PopHiber.Prepared || Context != &PopHiber.Context || Context->Target.StubPhysical == 0)
        return STATUS_INVALID_DEVICE_STATE;

    Status = PopHiber.Storage.Prepare(PopHiber.Storage.Context);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PopHiberBuildRanges();
    if (!NT_SUCCESS(Status))
        return Status;

    PopHiber.ExtentIndex = 0;
    PopHiber.ExtentStartVcn = PopHiber.Extents->StartingVcn.QuadPart;
    PopHiber.StagingUsed = 0;
    PopHiber.StagingFileOffset = PAGE_SIZE;

    PopHiber.RecordLength = 0;
    Status = PopHiberAppend(PopHiber.Ranges, (ULONG)(PopHiber.RangeCount * sizeof(LNT_HIBER_RANGE)));
    if (NT_SUCCESS(Status))
        Status = PopHiberFlush();
    if (!NT_SUCCESS(Status))
        return Status;

    RecordOffset = PopHiber.StagingFileOffset;
    PopHiber.RecordLength = 0;
    Status = PopHiberWriteRecords();
    if (NT_SUCCESS(Status))
        Status = PopHiberFlush();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = PopHiber.Storage.Flush(PopHiber.Storage.Context);
    if (!NT_SUCCESS(Status))
        return Status;

    DPRINT1("Hibernation image written: %I64u of %I64u pages saved in %I64u MB\n",
            PopHiber.DataPages, PopHiber.TargetPages, PopHiber.RecordLength >> 20);

    Header = (PLNT_HIBER_HEADER)PopHiber.Staging;
    RtlZeroMemory(Header, PAGE_SIZE);
    Header->Signature = LNT_HIBER_SIGNATURE;
    Header->Version = LNT_HIBER_VERSION;
    Header->HeaderSize = sizeof(LNT_HIBER_HEADER);
    Header->RangeTableOffset = PAGE_SIZE;
    Header->RangeCount = PopHiber.RangeCount;
    Header->RecordOffset = RecordOffset;
    Header->RecordLength = PopHiber.RecordLength;
    Header->TargetPages = PopHiber.TargetPages;
    Header->DataPages = PopHiber.DataPages;
    Header->HighestPage = PopHiber.HighestPage;
    Header->LowStubPhysical = Context->Target.StubPhysical;
    Header->LowStubPages = Context->Target.StubPages;
    Header->LowStubProt32Offset = Context->Target.Prot32Offset;
    Header->LowStubGdtOffset = Context->Target.GdtOffset;
    Header->LowStubGdtLimit = Context->Target.GdtLimit;
    Header->RuntimeRangeCount = PopHiber.RuntimeRangeCount;
    RtlCopyMemory(Header->RuntimeRanges, PopHiber.RuntimeRanges,
                  PopHiber.RuntimeRangeCount * sizeof(LNT_HIBER_RUNTIME_RANGE));
    Header->HeaderCrc = RtlComputeCrc32(0, (PUCHAR)Header, sizeof(LNT_HIBER_HEADER));

    PopHiber.StagingFileOffset = 0;
    PopHiber.StagingUsed = PAGE_SIZE;
    Status = PopHiberFlush();
    if (!NT_SUCCESS(Status))
        return Status;

    return PopHiber.Storage.Flush(PopHiber.Storage.Context);
}

static
VOID
PopHiberCloseFile(VOID)
{
    if (PopHiber.Extents)
    {
        ExFreePoolWithTag(PopHiber.Extents, POP_HIBER_TAG);
        PopHiber.Extents = NULL;
    }

    if (PopHiber.FileObject)
    {
        ObDereferenceObject(PopHiber.FileObject);
        PopHiber.FileObject = NULL;
    }

    if (PopHiber.FileHandle)
    {
        ZwClose(PopHiber.FileHandle);
        PopHiber.FileHandle = NULL;
    }

    PopHiber.Ready = FALSE;
    PopCapabilities.HiberFilePresent = FALSE;
}

static
NTSTATUS
PopHiberBuildPath(
    _Out_ PUNICODE_STRING Path)
{
    UNICODE_STRING LinkName = RTL_CONSTANT_STRING(L"\\SystemRoot");
    UNICODE_STRING FileName = RTL_CONSTANT_STRING(L"\\hiberfil.sys");
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Target;
    HANDLE LinkHandle;
    NTSTATUS Status;
    USHORT Index;

    InitializeObjectAttributes(&ObjectAttributes, &LinkName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenSymbolicLinkObject(&LinkHandle, SYMBOLIC_LINK_QUERY, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;

    Target.Length = 0;
    Target.MaximumLength = 512 * sizeof(WCHAR);
    Target.Buffer = ExAllocatePoolWithTag(PagedPool, Target.MaximumLength + FileName.Length, POP_HIBER_TAG);
    if (Target.Buffer == NULL)
    {
        ZwClose(LinkHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQuerySymbolicLinkObject(LinkHandle, &Target, NULL);
    ZwClose(LinkHandle);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Target.Buffer, POP_HIBER_TAG);
        return Status;
    }

    for (Index = Target.Length / sizeof(WCHAR); Index > 0 && Target.Buffer[Index - 1] != L'\\'; Index--);
    if (Index <= 1)
    {
        ExFreePoolWithTag(Target.Buffer, POP_HIBER_TAG);
        return STATUS_OBJECT_PATH_INVALID;
    }

    Target.Length = (Index - 1) * sizeof(WCHAR);
    Target.MaximumLength += FileName.Length;
    RtlAppendUnicodeStringToString(&Target, &FileName);
    *Path = Target;
    return STATUS_SUCCESS;
}

static
VOID
PopHiberReadPolicy(
    _Out_ PULONG Enabled,
    _Out_ PULONG Percent)
{
    RTL_QUERY_REGISTRY_TABLE Table[3];

    *Enabled = 1;
    *Percent = POP_HIBER_DEFAULT_PERCENT;

    RtlZeroMemory(Table, sizeof(Table));
    Table[0].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[0].Name = L"HibernateEnabled";
    Table[0].EntryContext = Enabled;
    Table[0].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;
    Table[1].Flags = RTL_QUERY_REGISTRY_DIRECT | RTL_QUERY_REGISTRY_TYPECHECK;
    Table[1].Name = L"HiberFileSizePercent";
    Table[1].EntryContext = Percent;
    Table[1].DefaultType = (REG_DWORD << RTL_QUERY_REGISTRY_TYPECHECK_SHIFT) | REG_NONE;

    RtlQueryRegistryValues(RTL_REGISTRY_CONTROL, L"Power", Table, NULL, NULL);

    if (*Percent == 0 || *Percent > 100)
        *Percent = POP_HIBER_DEFAULT_PERCENT;
}

static
NTSTATUS
PopHiberOpenFile(
    _In_ ULONG64 Size)
{
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    FILE_VALID_DATA_LENGTH_INFORMATION ValidData;
    FILE_FS_SIZE_INFORMATION SizeInfo;
    PARTITION_INFORMATION_EX PartitionInfo;
    STARTING_VCN_INPUT_BUFFER StartingVcn;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER AllocationSize;
    LARGE_INTEGER Offset;
    UNICODE_STRING Path;
    PDEVICE_OBJECT StorageDevice;
    PLNT_HIBER_HEADER Header;
    KEVENT Event;
    PIRP Irp;
    PVPB Vpb;
    NTSTATUS Status;

    Status = PopHiberBuildPath(&Path);
    if (!NT_SUCCESS(Status))
        return Status;

    AllocationSize.QuadPart = (LONGLONG)Size;
    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&PopHiber.FileHandle,
                          FILE_GENERIC_READ | FILE_GENERIC_WRITE | DELETE,
                          &ObjectAttributes,
                          &IoStatus,
                          &AllocationSize,
                          FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM,
                          0,
                          FILE_OPEN_IF,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE | FILE_NO_COMPRESSION,
                          NULL,
                          0);
    ExFreePoolWithTag(Path.Buffer, POP_HIBER_TAG);
    if (!NT_SUCCESS(Status))
    {
        PopHiber.FileHandle = NULL;
        return Status;
    }

    EndOfFile.EndOfFile.QuadPart = (LONGLONG)Size;
    Status = ZwSetInformationFile(PopHiber.FileHandle, &IoStatus, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
    if (!NT_SUCCESS(Status))
        goto Failure;

    ValidData.ValidDataLength.QuadPart = (LONGLONG)Size;
    Status = ZwSetInformationFile(PopHiber.FileHandle, &IoStatus, &ValidData, sizeof(ValidData), FileValidDataLengthInformation);
    if (!NT_SUCCESS(Status))
        DPRINT1("Setting the hibernation file valid data length failed: 0x%08lx\n", Status);

    Header = ExAllocatePoolZero(PagedPool, PAGE_SIZE, POP_HIBER_TAG);
    if (Header == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }

    Offset.QuadPart = 0;
    Status = ZwWriteFile(PopHiber.FileHandle, NULL, NULL, NULL, &IoStatus, Header, PAGE_SIZE, &Offset, NULL);
    ExFreePoolWithTag(Header, POP_HIBER_TAG);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = ZwFlushBuffersFile(PopHiber.FileHandle, &IoStatus);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = ObReferenceObjectByHandle(PopHiber.FileHandle, FILE_WRITE_DATA, IoFileObjectType, KernelMode,
                                       (PVOID *)&PopHiber.FileObject, NULL);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = ZwQueryVolumeInformationFile(PopHiber.FileHandle, &IoStatus, &SizeInfo, sizeof(SizeInfo), FileFsSizeInformation);
    if (!NT_SUCCESS(Status))
        goto Failure;

    PopHiber.ClusterSize = SizeInfo.BytesPerSector * SizeInfo.SectorsPerAllocationUnit;
    Vpb = PopHiber.FileObject->Vpb;
    if (Vpb == NULL && PopHiber.FileObject->DeviceObject != NULL)
        Vpb = PopHiber.FileObject->DeviceObject->Vpb;
    if (PopHiber.ClusterSize == 0 || Vpb == NULL || Vpb->RealDevice == NULL)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Failure;
    }

    StorageDevice = Vpb->RealDevice;
    RtlZeroMemory(&PartitionInfo, sizeof(PartitionInfo));
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_PARTITION_INFO_EX, StorageDevice, NULL, 0,
                                        &PartitionInfo, sizeof(PartitionInfo), FALSE, &Event, &IoStatus);
    if (Irp == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }
    Status = IoCallDriver(StorageDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (!NT_SUCCESS(Status))
        goto Failure;
    PopHiber.PartitionOffset = PartitionInfo.StartingOffset;

    RtlZeroMemory(&PopHiber.Storage, sizeof(PopHiber.Storage));
    PopHiber.Storage.BytesPerSector = SizeInfo.BytesPerSector;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_REACTOS_STORAGE_GET_DUMP_INTERFACE, StorageDevice,
                                        &PopHiber.Storage, sizeof(PopHiber.Storage),
                                        &PopHiber.Storage, sizeof(PopHiber.Storage), FALSE, &Event, &IoStatus);
    if (Irp == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }
    Status = IoCallDriver(StorageDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (!NT_SUCCESS(Status))
        goto Failure;

    if (PopHiber.Storage.Version != ROS_STORAGE_DUMP_INTERFACE_VERSION ||
        PopHiber.Storage.Size < sizeof(PopHiber.Storage) ||
        PopHiber.Storage.Context == NULL ||
        PopHiber.Storage.MaximumTransferLength == 0 ||
        PopHiber.Storage.Prepare == NULL ||
        PopHiber.Storage.WriteRoutine == NULL ||
        PopHiber.Storage.Flush == NULL ||
        (PopHiber.ClusterSize % PopHiber.Storage.BytesPerSector) != 0 ||
        (PAGE_SIZE % PopHiber.Storage.BytesPerSector) != 0)
    {
        Status = STATUS_REVISION_MISMATCH;
        goto Failure;
    }

    PopHiber.Extents = ExAllocatePoolWithTag(NonPagedPool, POP_HIBER_EXTENT_BUFFER, POP_HIBER_TAG);
    if (PopHiber.Extents == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }

    StartingVcn.StartingVcn.QuadPart = 0;
    Status = ZwFsControlFile(PopHiber.FileHandle, NULL, NULL, NULL, &IoStatus, FSCTL_GET_RETRIEVAL_POINTERS,
                             &StartingVcn, sizeof(StartingVcn), PopHiber.Extents, POP_HIBER_EXTENT_BUFFER);
    if (!NT_SUCCESS(Status))
        goto Failure;

    if (PopHiber.Extents->ExtentCount == 0 || PopHiber.Extents->StartingVcn.QuadPart != 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failure;
    }

    PopHiber.Capacity = Size;
    PopHiber.Ready = TRUE;
    PopCapabilities.HiberFilePresent = TRUE;
    DPRINT1("Hibernation file ready: %I64u MB in %lu extents\n", Size >> 20, PopHiber.Extents->ExtentCount);
    return STATUS_SUCCESS;

Failure:
    DPRINT1("Hibernation file setup failed: 0x%08lx\n", Status);
    PopHiberCloseFile();
    return Status;
}

static
NTSTATUS
PopHiberDeleteFile(VOID)
{
    FILE_DISPOSITION_INFORMATION Disposition;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatus;
    UNICODE_STRING Path;
    HANDLE Handle;
    NTSTATUS Status;

    PopHiberCloseFile();

    Status = PopHiberBuildPath(&Path);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&Handle, DELETE | SYNCHRONIZE, &ObjectAttributes, &IoStatus, 0,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    ExFreePoolWithTag(Path.Buffer, POP_HIBER_TAG);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    Disposition.DeleteFile = TRUE;
    Status = ZwSetInformationFile(Handle, &IoStatus, &Disposition, sizeof(Disposition), FileDispositionInformation);
    ZwClose(Handle);
    return Status;
}

NTSTATUS
NTAPI
PopHiberReserve(
    _In_ BOOLEAN Enable)
{
    ULONG Enabled;
    ULONG Percent;
    ULONG64 Size;
    NTSTATUS Status;

    PAGED_CODE();

    KeWaitForSingleObject(&PopHiber.Lock, Executive, KernelMode, FALSE, NULL);

    if (!Enable)
    {
        Status = PopHiberDeleteFile();
        KeReleaseMutex(&PopHiber.Lock, FALSE);
        return Status;
    }

    if (PopHiber.Ready)
    {
        KeReleaseMutex(&PopHiber.Lock, FALSE);
        return STATUS_SUCCESS;
    }

    PopHiberReadPolicy(&Enabled, &Percent);
    Size = ALIGN_UP_BY((ULONG64)MmNumberOfPhysicalPages * PAGE_SIZE * Percent / 100, 1024 * 1024);
    Status = PopHiberOpenFile(Size);
    if (!NT_SUCCESS(Status))
        PopHiberDeleteFile();

    KeReleaseMutex(&PopHiber.Lock, FALSE);
    return Status;
}

static
VOID
NTAPI
PopHiberInitializeWorker(
    _In_ PVOID Parameter)
{
    ULONG Enabled;
    ULONG Percent;

    UNREFERENCED_PARAMETER(Parameter);

    PopHiberReadPolicy(&Enabled, &Percent);
    if (Enabled)
        PopHiberReserve(TRUE);
    else
        PopHiberReserve(FALSE);
}

static
VOID
PopHiberQueueStart(
    _In_ LONG Bit)
{
    LONG Old = InterlockedOr(&PopHiberStartState, Bit);
    LONG Ready = POP_HIBER_START_CAPTURED | POP_HIBER_START_PLATFORM;

    if (((Old | Bit) & Ready) != Ready || (Old & POP_HIBER_START_QUEUED))
        return;

    if (InterlockedOr(&PopHiberStartState, POP_HIBER_START_QUEUED) & POP_HIBER_START_QUEUED)
        return;

    ExQueueWorkItem(&PopHiber.WorkItem, DelayedWorkQueue);
}

VOID
NTAPI
PopHiberInitialize(VOID)
{
    KeInitializeMutex(&PopHiber.Lock, 0);
    ExInitializeWorkItem(&PopHiber.WorkItem, PopHiberInitializeWorker, NULL);
}

VOID
NTAPI
PopHiberPlatformSupported(VOID)
{
    PopHiberQueueStart(POP_HIBER_START_PLATFORM);
}

VOID
NTAPI
PopHiberCaptureLoaderBlock(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;
    PLNT_EFI_MEMORY_DESCRIPTOR Efi;
    PLIST_ENTRY Entry;
    PUCHAR Map;
    ULONG Size;
    ULONG Stride;
    ULONG Offset;

    for (Entry = LoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &LoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        Descriptor = CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);
        if (Descriptor->MemoryType != LoaderHALCachedMemory || Descriptor->PageCount == 0)
            continue;

        if (PopHiber.HalRangeCount == POP_HIBER_MAX_HAL_RANGES)
        {
            DPRINT1("Too many HAL memory ranges for hibernation\n");
            PopHiber.HalRangeCount = 0;
            PopHiber.RuntimeRangeCount = 0;
            return;
        }

        PopHiber.HalRanges[PopHiber.HalRangeCount].BasePage = Descriptor->BasePage;
        PopHiber.HalRanges[PopHiber.HalRangeCount].PageCount = Descriptor->PageCount;
        PopHiber.HalRangeCount++;
    }

    if (LoaderBlock->FirmwareInformation.FirmwareTypeEfi)
    {
        Map = LoaderBlock->FirmwareInformation.u.EfiInformation.EfiMemoryMap;
        Size = LoaderBlock->FirmwareInformation.u.EfiInformation.EfiMemoryMapSize;
        Stride = LoaderBlock->FirmwareInformation.u.EfiInformation.EfiMemoryMapDescriptorSize;

        if (Map != NULL && Stride >= sizeof(LNT_EFI_MEMORY_DESCRIPTOR))
        {
            for (Offset = 0; Offset + Stride <= Size; Offset += Stride)
            {
                Efi = (PLNT_EFI_MEMORY_DESCRIPTOR)(Map + Offset);
                if (!(Efi->Attribute & LNT_EFI_MEMORY_RUNTIME) || Efi->NumberOfPages == 0)
                    continue;

                if (PopHiber.RuntimeRangeCount == LNT_HIBER_MAX_RUNTIME_RANGES)
                {
                    DPRINT1("Too many firmware runtime ranges for hibernation\n");
                    PopHiber.RuntimeRangeCount = 0;
                    return;
                }

                PopHiber.RuntimeRanges[PopHiber.RuntimeRangeCount].PhysicalStart = Efi->PhysicalStart;
                PopHiber.RuntimeRanges[PopHiber.RuntimeRangeCount].VirtualStart = Efi->VirtualStart;
                PopHiber.RuntimeRanges[PopHiber.RuntimeRangeCount].PageCount = Efi->NumberOfPages;
                PopHiber.RuntimeRanges[PopHiber.RuntimeRangeCount].Attribute = Efi->Attribute;
                PopHiber.RuntimeRanges[PopHiber.RuntimeRangeCount].Type = Efi->Type;
                PopHiber.RuntimeRangeCount++;
            }

            PopHiberQueueStart(POP_HIBER_START_CAPTURED);
        }
    }
}

BOOLEAN
NTAPI
PopHiberIsReady(VOID)
{
    return PopHiber.Ready;
}

NTSTATUS
NTAPI
PopHiberPrepare(
    _Out_ PVOID *SystemContext)
{
    PHYSICAL_ADDRESS Lowest;
    PHYSICAL_ADDRESS Highest;
    PHYSICAL_ADDRESS Boundary;

    PAGED_CODE();

    *SystemContext = NULL;
    if (!PopHiber.Ready)
        return STATUS_NOT_SUPPORTED;

    Lowest.QuadPart = 0;
    Highest.QuadPart = -1;
    Boundary.QuadPart = 0;
    PopHiber.Staging = MmAllocateContiguousMemorySpecifyCache(POP_HIBER_STAGING_SIZE, Lowest, Highest, Boundary, MmCached);
    PopHiber.Chunk = ExAllocatePoolWithTag(NonPagedPool, POP_HIBER_CHUNK_SIZE, POP_HIBER_TAG);
    PopHiber.LzOutput = ExAllocatePoolWithTag(NonPagedPool, POP_HIBER_LZ_CAPACITY, POP_HIBER_TAG);
    PopHiber.LzTable = ExAllocatePoolWithTag(NonPagedPool, sizeof(USHORT) << POP_HIBER_LZ_HASH_BITS, POP_HIBER_TAG);
    PopHiber.Ranges = ExAllocatePoolWithTag(NonPagedPool, POP_HIBER_MAX_RANGES * sizeof(LNT_HIBER_RANGE), POP_HIBER_TAG);

    if (!PopHiber.Staging || !PopHiber.Chunk || !PopHiber.LzOutput || !PopHiber.LzTable || !PopHiber.Ranges)
    {
        PopHiberRelease();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PopHiber.StagingPhysical = MmGetPhysicalAddress(PopHiber.Staging);
    RtlZeroMemory(&PopHiber.Context, sizeof(PopHiber.Context));
    PopHiber.Prepared = TRUE;
    *SystemContext = &PopHiber.Context;
    return STATUS_SUCCESS;
}

VOID
NTAPI
PopHiberRelease(VOID)
{
    PopHiber.Prepared = FALSE;

    if (PopHiber.Staging)
    {
        MmFreeContiguousMemorySpecifyCache(PopHiber.Staging, POP_HIBER_STAGING_SIZE, MmCached);
        PopHiber.Staging = NULL;
    }
    if (PopHiber.Chunk)
    {
        ExFreePoolWithTag(PopHiber.Chunk, POP_HIBER_TAG);
        PopHiber.Chunk = NULL;
    }
    if (PopHiber.LzOutput)
    {
        ExFreePoolWithTag(PopHiber.LzOutput, POP_HIBER_TAG);
        PopHiber.LzOutput = NULL;
    }
    if (PopHiber.LzTable)
    {
        ExFreePoolWithTag(PopHiber.LzTable, POP_HIBER_TAG);
        PopHiber.LzTable = NULL;
    }
    if (PopHiber.Ranges)
    {
        ExFreePoolWithTag(PopHiber.Ranges, POP_HIBER_TAG);
        PopHiber.Ranges = NULL;
    }
}

VOID
NTAPI
PopHiberInvalidate(VOID)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    PVOID Header;
    NTSTATUS Status;

    PAGED_CODE();

    if (!PopHiber.Ready)
        return;

    Header = ExAllocatePoolZero(PagedPool, PAGE_SIZE, POP_HIBER_TAG);
    if (Header == NULL)
        return;

    Offset.QuadPart = 0;
    Status = ZwWriteFile(PopHiber.FileHandle, NULL, NULL, NULL, &IoStatus, Header, PAGE_SIZE, &Offset, NULL);
    if (NT_SUCCESS(Status))
        Status = ZwFlushBuffersFile(PopHiber.FileHandle, &IoStatus);
    if (!NT_SUCCESS(Status))
        DPRINT1("Invalidating the hibernation image failed: 0x%08lx\n", Status);

    ExFreePoolWithTag(Header, POP_HIBER_TAG);
}
