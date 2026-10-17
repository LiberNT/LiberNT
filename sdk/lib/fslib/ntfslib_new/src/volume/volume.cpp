/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static BOOLEAN
IsValidRecordSize(_In_ INT8 ClustersPerRecord,
                  _In_ ULONG ClusterSize)
{
    ULONG RecordSize;

    if (ClustersPerRecord == 0)
        return FALSE;

    if (ClustersPerRecord < 0)
    {
        ULONG Exponent = (ULONG)(-ClustersPerRecord);
        if (Exponent >= 32)
            return FALSE;
        RecordSize = 1UL << Exponent;
    }
    else
    {
        if ((ULONG)ClustersPerRecord > MAXULONG / ClusterSize)
            return FALSE;
        RecordSize = (ULONG)ClustersPerRecord * ClusterSize;
    }

    return RecordSize >= 512 && RecordSize <= 65536 &&
           (RecordSize % sizeof(ULONGLONG)) == 0;
}

EXTERN_C
NTSTATUS
NtfsProbePartition(
    _In_ ULONG BytesPerSector,
    _In_ PUCHAR BootSectorData)
{
    BootSector* PartitionBootSector;
    ULONG ClusterSize;
    ULONGLONG ClusterCount;
    USHORT i;

    if (!BootSectorData)
        return STATUS_INVALID_PARAMETER;

    // Check bytes per sector.
    if (BytesPerSector != 512
        && BytesPerSector != 4096)
    {
        /* NOTE:
         * Per Microsoft's documentation, bytes per sector can either be
         * 4096 bytes or 512 bytes.
         *
         * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-iobuildsynchronousfsdrequest
         */
        DPRINT1("Volume has invalid sector size! (%ld bytes)\n", BytesPerSector);
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    PartitionBootSector = (BootSector*)BootSectorData;

    // Check if OEM_ID is "NTFS    ".
    if (RtlCompareMemory(PartitionBootSector->OEM_ID, "NTFS    ", 8) != 8)
    {
        /* Mount requests are sent to every registered file system. Rejecting
         * a non-NTFS volume here is expected and must not pollute the log. */
        if (RtlCompareMemory(PartitionBootSector->OEM_ID, "-FVE-FS-", 8) == 8)
            DPRINT1("Volume is encrypted with BitLocker and cannot be mounted\n");
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    // Check if Reserved0 is NULL.
    for (i = 0; i < 7; i++)
    {
        if (PartitionBootSector->Reserved0[i] != 0)
        {
            DPRINT1("Failed in field Reserved0: byte %lu is 0x%02x\n", (unsigned long)i, (unsigned int)(unsigned char)PartitionBootSector->Reserved0[i]);
            return STATUS_UNRECOGNIZED_VOLUME;
        }
    }

    /* Check if Reserved3 is NULL.
     * Note: Reserved3 is only 4 bytes; reading 7 like Reserved0 walks into
     * the following (nonzero) field, which is why this check used to fail.
     */
    for (i = 0; i < 4; i++)
    {
        if (PartitionBootSector->Reserved3[i] != 0)
        {
            DPRINT1("Failed in field Reserved3: [%.4s]\n", PartitionBootSector->Reserved3);
            return STATUS_UNRECOGNIZED_VOLUME;
        }
    }

    // Check cluster size.
    ClusterSize = PartitionBootSector->BytesPerSector
                * PartitionBootSector->SectorsPerCluster;

    if (ClusterSize != 512 && ClusterSize != 1024 &&
        ClusterSize != 2048 && ClusterSize != 4096 &&
        ClusterSize != 8192 && ClusterSize != 16384 &&
        ClusterSize != 32768 && ClusterSize != 65536)
    {
        DPRINT1("Cluster size failed: %hu, %hu, %hu\n",
                PartitionBootSector->BytesPerSector,
                PartitionBootSector->SectorsPerCluster,
                ClusterSize);
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    ClusterCount =
        PartitionBootSector->SectorsInVolume /
        PartitionBootSector->SectorsPerCluster;
    if (ClusterCount == 0 ||
        PartitionBootSector->MFTLCN >= ClusterCount ||
        PartitionBootSector->MFTMirrLCN >= ClusterCount)
    {
        DPRINT1("Volume has invalid sector count or MFT locations!\n");
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    if (!IsValidRecordSize(PartitionBootSector->ClustersPerFileRecord,
                           ClusterSize) ||
        !IsValidRecordSize(PartitionBootSector->ClustersPerIndexRecord,
                           ClusterSize))
    {
        DPRINT1("Volume has invalid file or index record sizes!\n");
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    // We are NTFS.
#ifdef NTFS_DEBUG
    PrintNTFSBootSector(PartitionBootSector);
#endif
    return STATUS_SUCCESS;
}

EXTERN_C
NTSTATUS
NtfsProbePartitionAndOpenVolume(
    _In_ ULONG BytesPerSector,
    _Out_ PNtfsVolume* VolumeOut)
{
    return NtfsProbePartitionAndOpenVolumeEx(BytesPerSector,
                                             NULL,
                                             VolumeOut);
}

EXTERN_C
NTSTATUS
NtfsProbePartitionAndOpenVolumeEx(
    _In_ ULONG BytesPerSector,
    _In_opt_ void* IoContext,
    _Out_ PNtfsVolume* VolumeOut)
{
    NTSTATUS Status;
    PVolume VolumeObject;
    PUCHAR BootSectorData;

    if (!VolumeOut || BytesPerSector == 0)
        return STATUS_INVALID_PARAMETER;

    // Make sure volume pointer is null if we fail for any reason.
    *VolumeOut = NULL;

    // Read the boot sector through the environment's disk routines.
    BootSectorData = new(NonPagedPool) UCHAR[BytesPerSector];
    if (!BootSectorData)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfsReadVolumeContext(IoContext,
                                   BytesPerSector,
                                   0,
                                   BytesPerSector,
                                   BootSectorData);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to read the boot sector! (Status %lx)\n", Status);
        goto Done;
    }

    Status = NtfsProbePartition(BytesPerSector, BootSectorData);
    if (!NT_SUCCESS(Status))
        goto Done;

    // Initialize the volume object.
    VolumeObject = new(NonPagedPool) Volume();
    if (!VolumeObject)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    Status = VolumeObject->Initialize(BootSectorData,
                                      IoContext);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to initialize volume object! (Status %lx)\n", Status);
        delete VolumeObject;
        goto Done;
    }

    *VolumeOut = reinterpret_cast<PNtfsVolume>(VolumeObject);

Done:
    delete[] BootSectorData;
    return Status;
}

Volume::~Volume()
{
    delete CachedBitmapFile;
    CachedBitmapFile = NULL;
    CachedBitmapData = NULL;
    delete[] BitmapWorkBuffer;
    BitmapWorkBuffer = NULL;
    while (RecordPoolHead)
    {
        PUCHAR Next = *(PUCHAR*)RecordPoolHead;

        delete[] RecordPoolHead;
        RecordPoolHead = Next;
    }
    RecordPoolCount = 0;
    delete[] IndexWorkBuffer;
    IndexWorkBuffer = NULL;

    delete MFT;
    delete LFS;
    delete[] AttrDefCache;
    delete[] UpcaseTable;
}

static
BOOLEAN
VolumeIsHibernated(_In_ PVolume DiskVolume)
{
    WCHAR Name[] = L"hiberfil.sys";
    PFileRecord File = NULL;
    UCHAR Signature[4];
    ULONG Remaining = sizeof(Signature);
    BOOLEAN Hibernated = FALSE;

    if (!NT_SUCCESS(DiskVolume->MFT->GetFileRecordFromQuery(Name, &File)))
        return FALSE;
    if (NT_SUCCESS(File->CopyData(TypeData, NULL, Signature, &Remaining)) && Remaining == 0)
    {
        Hibernated = (Signature[0] == 'h' && Signature[1] == 'i' && Signature[2] == 'b' && Signature[3] == 'r') ||
                     (Signature[0] == 'H' && Signature[1] == 'I' && Signature[2] == 'B' && Signature[3] == 'R');
    }
    delete File;
    return Hibernated;
}

NTSTATUS
Volume::Initialize(_In_ PUCHAR BootSectorData,
                   _In_opt_ void* Context)
{
    NtfsInitializeMutex(&RecordPoolMutex);

    BootSector* PartitionBootSector;
    PVolumeInformationEx VolumeInfo;
    PAttribute VolumeInfoAttribute;
    PFileRecord VolumeFile;
    NTSTATUS Status;

    PartitionBootSector = (BootSector*)BootSectorData;
    IoContext = Context;
    VolumeFile = NULL;
    ShowMetadataFiles = NtfsDefaultShowMetadataFiles;
    Generate8dot3Names = NtfsDefaultGenerate8dot3Names;
    IsReadOnly = NtfsDefaultReadOnlyMode;

    // Pull in relevant information from the boot sector.
    BytesPerSector = PartitionBootSector->BytesPerSector;
    SectorsPerCluster = PartitionBootSector->SectorsPerCluster;
    SectorsInVolume = PartitionBootSector->SectorsInVolume;
    ClustersInVolume =
        SectorsInVolume / PartitionBootSector->SectorsPerCluster;
    ClustersPerIndexRecord = PartitionBootSector->ClustersPerIndexRecord;
    SerialNumber = PartitionBootSector->SerialNumber;

    // Initialize Master File Table
    MFT = new(PagedPool, TAG_MFT) MasterFileTable(this,
                                                  PartitionBootSector->MFTLCN,
                                                  PartitionBootSector->MFTMirrLCN,
                                                  PartitionBootSector->ClustersPerFileRecord);
    if (!MFT)
        return STATUS_INSUFFICIENT_RESOURCES;

    // Get the NTFS Major and Minor versions from $Volume.
    Status = MFT->GetFileAttributeFromFileRecordNumber(TypeVolumeInformation,
                                                       NULL,
                                                       _Volume,
                                                       &VolumeFile,
                                                       &VolumeInfoAttribute);

    if (!NT_SUCCESS(Status))
        return Status;

    VolumeInfo = (PVolumeInformationEx)GetResidentDataPointer(VolumeInfoAttribute);

    NtfsMajorVersion = VolumeInfo->MajorVersion;
    NtfsMinorVersion = VolumeInfo->MinorVersion;
    delete VolumeFile;
    VolumeFile = NULL;

    /*
     * Diagnostic readers do not need journal replay. In particular, freshly
     * formatted images may contain an uninitialized (all-0xff) $LogFile,
     * which is valid for read-only inspection but has no restart page to
     * parse.
     */
    if (IsReadOnly)
        return STATUS_SUCCESS;

    if (VolumeIsHibernated(this))
    {
        DPRINT1("The volume holds a hibernated system. Opening disk as readonly.\n");
        IsReadOnly = TRUE;
        return STATUS_SUCCESS;
    }

    // Initialize Log File Service
    LFS = new(PagedPool, TAG_LOG_FILE_SERVICE) LogFileService(this);

    if (!LFS)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = LFS->InitializeLFS();

    if (Status == STATUS_LOG_BLOCK_VERSION)
    {
        /* The version of LFS is incompatible with our LFS.
        * Open volume as readonly.
        */
        DPRINT1("LFS version is incompatible. Opening disk as readonly.\n");
        IsReadOnly = TRUE;
        Status = STATUS_SUCCESS;
    }
    else if (Status == STATUS_VOLUME_DIRTY)
    {
        DPRINT1("Log recovery failed. Opening disk as readonly.\n");
        IsReadOnly = TRUE;
        Status = STATUS_SUCCESS;
    }
    else if (NT_SUCCESS(Status) && LFS->VolumeRecovered)
    {
        delete MFT;
        MFT = new(PagedPool, TAG_MFT) MasterFileTable(this,
                                                      PartitionBootSector->MFTLCN,
                                                      PartitionBootSector->MFTMirrLCN,
                                                      PartitionBootSector->ClustersPerFileRecord);
        if (!MFT)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

NTSTATUS
Volume::ReloadSize()
{
    BootSector* PartitionBootSector;
    PUCHAR BootSectorData;
    NTSTATUS Status;

    BootSectorData = new(NonPagedPool) UCHAR[BytesPerSector];
    if (!BootSectorData)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfsReadVolumeContext(IoContext,
                                   BytesPerSector,
                                   0,
                                   BytesPerSector,
                                   BootSectorData);
    if (NT_SUCCESS(Status))
        Status = NtfsProbePartition(BytesPerSector, BootSectorData);
    if (NT_SUCCESS(Status))
    {
        PartitionBootSector = (BootSector*)BootSectorData;
        if (PartitionBootSector->BytesPerSector != BytesPerSector ||
            PartitionBootSector->SectorsPerCluster != SectorsPerCluster)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
        }
        else
        {
            delete CachedBitmapFile;
            CachedBitmapFile = NULL;
            CachedBitmapData = NULL;
            ReservedClustersKnown = FALSE;
            SectorsInVolume = PartitionBootSector->SectorsInVolume;
            ClustersInVolume = SectorsInVolume / SectorsPerCluster;
        }
    }

    delete[] BootSectorData;
    return Status;
}

NTSTATUS
Volume::QueryInformation(
    _Out_ PNtfsVolumeInformation Information)
{
    LARGE_INTEGER FreeClusters;
    ULONG ClusterSize;
    NTSTATUS Status;

    if (!Information)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Information, sizeof(*Information));
    if (!MFT || BytesPerSector == 0 ||
        SectorsPerCluster == 0)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    ClusterSize =
        BytesPerSector * (ULONG)SectorsPerCluster;
    if (ClusterSize == 0)
        return STATUS_INVALID_DEVICE_STATE;

    Status = MFT->QueryVolumeInformation(Information);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = GetFreeClusters(&FreeClusters);
    if (!NT_SUCCESS(Status))
        return Status;
    if (FreeClusters.QuadPart < 0)
        return STATUS_FILE_CORRUPT_ERROR;

    Information->VolumeSerialNumber = SerialNumber;
    Information->NumberSectors = SectorsInVolume;
    Information->TotalClusters = ClustersInVolume;
    Information->FreeClusters =
        (ULONGLONG)FreeClusters.QuadPart;
    Information->BytesPerSector = BytesPerSector;
    Information->BytesPerCluster = ClusterSize;
    Information->ClustersPerFileRecordSegment =
        Information->BytesPerFileRecordSegment /
        ClusterSize;
    Information->MajorVersion = NtfsMajorVersion;
    Information->MinorVersion = NtfsMinorVersion;
    Information->TotalReserved = GetReservedClusters();
    return STATUS_SUCCESS;
}

ULONGLONG
Volume::GetReservedClusters()
{
    if (!ReservedClustersKnown)
    {
        ReservedClusters = ComputeReservedClusters();
        ReservedClustersKnown = TRUE;
    }
    return ReservedClusters < ClustersInVolume ? ReservedClusters : ClustersInVolume;
}

ULONGLONG
Volume::ComputeReservedClusters()
{
    WCHAR Name[] = L"$SRAT";
    PFileRecord File = NULL;
    PAttribute Attribute;
    PUCHAR Table;
    ULONGLONG Reserved = 0;
    ULONG Length, Count, EntryOffset, EntrySize, UsedCount, UsedOffset, Index;

    if (!MFT || !NT_SUCCESS(MFT->GetFileRecord(_Bitmap, &File)))
        return 0;

    Attribute = File->GetAttribute(TypeData, Name);
    if (Attribute && !Attribute->IsNonResident &&
        Attribute->Resident.DataLength >= NTFS_SRAT_HEADER_SIZE)
    {
        Table = (PUCHAR)GetResidentDataPointer(Attribute);
        Length = Attribute->Resident.DataLength;
        Count = Table[NTFS_SRAT_COUNT_OFFSET];
        EntryOffset = *(PUSHORT)(Table + NTFS_SRAT_ENTRY_OFFSET);
        EntrySize = *(PUSHORT)(Table + NTFS_SRAT_ENTRY_SIZE_OFFSET);
        UsedCount = *(PULONG)(Table + NTFS_SRAT_USED_COUNT_OFFSET);
        UsedOffset = EntryOffset + Count * EntrySize;

        if (EntryOffset >= NTFS_SRAT_HEADER_SIZE &&
            EntrySize >= NTFS_SRAT_ENTRY_MIN_SIZE &&
            UsedCount <= NTFS_SRAT_MAX_IDS &&
            (ULONGLONG)UsedOffset + (ULONGLONG)UsedCount * sizeof(ULONG) == Length)
        {
            for (Index = 0; Index < Count; Index++)
            {
                PUCHAR Entry = Table + EntryOffset + Index * EntrySize;
                ULONG Id = *(PULONG)Entry;
                ULONG Guarantee = *(PULONG)(Entry + NTFS_SRAT_GUARANTEE_OFFSET);
                ULONG Used = Id < UsedCount ? *(PULONG)(Table + UsedOffset + Id * sizeof(ULONG)) : 0;

                if (Guarantee > Used)
                    Reserved += Guarantee - Used;
            }
        }
    }

    delete File;

    Reserved += GetJournalReserveClusters();
    Reserved += NTFS_BASELINE_RESERVE_BYTES / ((ULONG)BytesPerSector * SectorsPerCluster);
    return Reserved;
}

ULONGLONG
Volume::GetJournalReserveClusters()
{
    WCHAR JournalName[] = L"$UsnJrnl";
    WCHAR MaxName[] = L"$Max";
    PFileRecord Extend = NULL;
    PFileRecord Journal = NULL;
    PAttribute Attribute;
    PUCHAR Value;
    ULONGLONG Clusters = 0;
    ULONGLONG MaximumSize, AllocationDelta;
    ULONG ClusterSize = (ULONG)BytesPerSector * SectorsPerCluster;

    if (!MFT || ClusterSize == 0 || !NT_SUCCESS(MFT->GetFileRecord(_Extend, &Extend)))
        return 0;

    if (NT_SUCCESS(MFT->GetFileRecordInDirectory(Extend, JournalName, &Journal)))
    {
        Attribute = Journal->GetAttribute(TypeData, MaxName);
        if (Attribute && !Attribute->IsNonResident &&
            Attribute->Resident.DataLength >= 2 * sizeof(ULONGLONG))
        {
            Value = (PUCHAR)GetResidentDataPointer(Attribute);
            MaximumSize = *(PULONGLONG)Value;
            AllocationDelta = *(PULONGLONG)(Value + sizeof(ULONGLONG));
            if (MaximumSize <= ClustersInVolume * ClusterSize &&
                AllocationDelta <= ClustersInVolume * ClusterSize)
            {
                Clusters = (MaximumSize + 2 * AllocationDelta) / ClusterSize;
                Clusters += Clusters / NTFS_JOURNAL_RESERVE_OVERHEAD_DIVISOR;
            }
        }
        delete Journal;
    }

    delete Extend;
    return Clusters;
}
