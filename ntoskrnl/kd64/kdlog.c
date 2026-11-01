/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Write the kernel debug print log to the boot volume
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <ntdddisk.h>
#define NDEBUG
#include <debug.h>
#include <reactos/drivers/dumpstor.h>

#define KDP_BOOT_LOG_CHUNK_SIZE (64 * 1024)
#define KDP_BOOT_LOG_POLL_MS 250
#define KDP_BOOT_LOG_RESERVE_SIZE (128 * 1024)
#define KDP_BOOT_LOG_STOP_TIMEOUT_MS 5000

typedef struct _KDP_BOOT_LOG_STATE
{
    ULONG64 Written;
    ULONG64 RingPosition;
    ULONG64 ReserveEnd;
    PRETRIEVAL_POINTERS_BUFFER Extents;
    UCHAR Tail[PAGE_SIZE];
} KDP_BOOT_LOG_STATE, *PKDP_BOOT_LOG_STATE;

static CHAR KdpBootLogName[128];
static CHAR KdpBootLogBuffer[KDP_BOOT_LOG_CHUNK_SIZE];
static UCHAR KdpBootLogZeroes[KDP_BOOT_LOG_CHUNK_SIZE];
static ULONG64 KdpBootLogExtentBuffers[2][PAGE_SIZE / sizeof(ULONG64)];
static KDP_BOOT_LOG_STATE KdpBootLogStates[2];
static PKDP_BOOT_LOG_STATE volatile KdpBootLogPublished;
static HANDLE KdpBootLogHandle;
static PDEVICE_OBJECT KdpBootLogDevice;
static LARGE_INTEGER KdpBootLogPartitionOffset;
static ULONG KdpBootLogClusterSize;
static ULONG KdpBootLogSectorSize;
static PUCHAR KdpBootLogCrashBuffer;
static ROS_STORAGE_DUMP_INTERFACE KdpBootLogStorage;
static BOOLEAN KdpBootLogStorageReady;
static BOOLEAN KdpBootLogStoragePrepared;
static KEVENT KdpBootLogWake;
static KEVENT KdpBootLogStopped;
static volatile BOOLEAN KdpBootLogStopping;

static ULONG64 KdpBootLogWritten;
static ULONG64 KdpBootLogPosition;
static ULONG64 KdpBootLogReserveEnd;
static PRETRIEVAL_POINTERS_BUFFER KdpBootLogExtents;
static UCHAR KdpBootLogTail[PAGE_SIZE];

static NTSTATUS KdpBootLogAppend(_In_reads_bytes_(Length) PCHAR Data, _In_ ULONG Length)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    ULONG Keep;
    NTSTATUS Status;

    Offset.QuadPart = KdpBootLogWritten;
    Status = ZwWriteFile(KdpBootLogHandle, NULL, NULL, NULL, &IoStatus, Data, Length, &Offset, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    KdpBootLogWritten += Length;
    if (KdpBootLogSectorSize != 0)
    {
        Keep = (ULONG)(KdpBootLogWritten % KdpBootLogSectorSize);
        if (Keep <= Length)
            RtlCopyMemory(KdpBootLogTail, Data + Length - Keep, Keep);
        else
            RtlCopyMemory(KdpBootLogTail + Keep - Length, Data, Length);
    }

    return STATUS_SUCCESS;
}

static VOID KdpBootLogExtendReserve(VOID)
{
    STARTING_VCN_INPUT_BUFFER StartingVcn;
    PRETRIEVAL_POINTERS_BUFFER Extents;
    PKDP_BOOT_LOG_STATE Published;
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    ULONG64 Target;
    ULONG Length;
    NTSTATUS Status;

    if (KdpBootLogReserveEnd < KdpBootLogWritten)
        KdpBootLogReserveEnd = KdpBootLogWritten;

    Target = KdpBootLogWritten + 2 * KDP_BOOT_LOG_RESERVE_SIZE + KdpBootLogClusterSize - 1;
    Target -= Target % KdpBootLogClusterSize;
    while (KdpBootLogReserveEnd < Target)
    {
        Length = (ULONG)min(Target - KdpBootLogReserveEnd, sizeof(KdpBootLogZeroes));
        Offset.QuadPart = KdpBootLogReserveEnd;
        Status = ZwWriteFile(KdpBootLogHandle, NULL, NULL, NULL, &IoStatus, KdpBootLogZeroes, Length, &Offset, NULL);
        if (!NT_SUCCESS(Status))
        {
            KdpBootLogExtents = NULL;
            return;
        }
        KdpBootLogReserveEnd += Length;
    }

    Published = KdpBootLogPublished;
    Extents = (PRETRIEVAL_POINTERS_BUFFER)KdpBootLogExtentBuffers[0];
    if ((Published != NULL) && (Published->Extents == Extents))
        Extents = (PRETRIEVAL_POINTERS_BUFFER)KdpBootLogExtentBuffers[1];

    StartingVcn.StartingVcn.QuadPart = 0;
    Status = ZwFsControlFile(KdpBootLogHandle, NULL, NULL, NULL, &IoStatus, FSCTL_GET_RETRIEVAL_POINTERS, &StartingVcn, sizeof(StartingVcn), Extents, sizeof(KdpBootLogExtentBuffers[0]));
    if (!NT_SUCCESS(Status) || (Status == STATUS_BUFFER_OVERFLOW) || (Extents->ExtentCount == 0) || (Extents->StartingVcn.QuadPart != 0))
    {
        KdpBootLogExtents = NULL;
        return;
    }

    KdpBootLogExtents = Extents;
}

static VOID KdpBootLogPublish(VOID)
{
    PKDP_BOOT_LOG_STATE State;

    if (KdpBootLogExtents == NULL)
    {
        InterlockedExchangePointer((PVOID volatile *)&KdpBootLogPublished, NULL);
        return;
    }

    State = (KdpBootLogPublished == &KdpBootLogStates[0]) ? &KdpBootLogStates[1] : &KdpBootLogStates[0];
    State->Written = KdpBootLogWritten;
    State->RingPosition = KdpBootLogPosition;
    State->ReserveEnd = KdpBootLogReserveEnd;
    State->Extents = KdpBootLogExtents;
    RtlCopyMemory(State->Tail, KdpBootLogTail, (ULONG)(KdpBootLogWritten % KdpBootLogSectorSize));
    InterlockedExchangePointer((PVOID volatile *)&KdpBootLogPublished, State);
}

static VOID KdpBootLogTrim(VOID)
{
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    IO_STATUS_BLOCK IoStatus;

    InterlockedExchangePointer((PVOID volatile *)&KdpBootLogPublished, NULL);
    EndOfFile.EndOfFile.QuadPart = KdpBootLogWritten;
    ZwSetInformationFile(KdpBootLogHandle, &IoStatus, &EndOfFile, sizeof(EndOfFile), FileEndOfFileInformation);
    KdpBootLogReserveEnd = KdpBootLogWritten;
}

static VOID NTAPI KdpBootLogThread(_In_ PVOID Context)
{
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Interval;
    CHAR Notice[96];
    ULONG64 ReserveEnd;
    ULONG64 Lost;
    ULONG Length;
    BOOLEAN Dirty = FALSE;
    BOOLEAN Trimmed = FALSE;

    UNREFERENCED_PARAMETER(Context);

    Interval.QuadPart = -10000LL * KDP_BOOT_LOG_POLL_MS;
    for (;;)
    {
        Length = KdpCopyPrintBufferFrom(&KdpBootLogPosition, KdpBootLogBuffer, sizeof(KdpBootLogBuffer), &Lost);
        Dirty = Dirty || (Lost != 0) || (Length != 0);
        if (Lost != 0)
        {
            RtlStringCbPrintfA(Notice, sizeof(Notice), "\n[%I64u bytes of log output were lost before they could be written]\n", Lost);
            KdpBootLogAppend(Notice, (ULONG)strlen(Notice));
        }

        if (Length != 0)
            KdpBootLogAppend(KdpBootLogBuffer, Length);

        if (KdpBootLogStopping)
        {
            if (!Trimmed)
            {
                if (KdpBootLogDevice != NULL)
                    KdpBootLogTrim();
                Trimmed = TRUE;
                Dirty = TRUE;
            }
        }
        else if (KdpBootLogDevice != NULL)
        {
            if ((KdpBootLogExtents == NULL) || (KdpBootLogWritten + KDP_BOOT_LOG_RESERVE_SIZE > KdpBootLogReserveEnd))
            {
                ReserveEnd = KdpBootLogReserveEnd;
                KdpBootLogExtendReserve();
                Dirty = Dirty || (KdpBootLogReserveEnd != ReserveEnd);
            }
        }

        if (Dirty && NT_SUCCESS(ZwFlushBuffersFile(KdpBootLogHandle, &IoStatus)))
            Dirty = FALSE;

        if (Trimmed)
            KeSetEvent(&KdpBootLogStopped, IO_NO_INCREMENT, FALSE);
        else if (!Dirty && (KdpBootLogDevice != NULL))
            KdpBootLogPublish();

        if (Length == sizeof(KdpBootLogBuffer))
            continue;

        KeWaitForSingleObject(&KdpBootLogWake, Executive, KernelMode, FALSE, &Interval);
    }
}

static NTSTATUS KdpBootLogQueryDevice(VOID)
{
    PARTITION_INFORMATION_EX PartitionInfo;
    FILE_FS_SIZE_INFORMATION SizeInfo;
    IO_STATUS_BLOCK IoStatus;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT Device;
    KEVENT Event;
    PVPB Vpb;
    PIRP Irp;
    NTSTATUS Status;

    Status = ZwQueryVolumeInformationFile(KdpBootLogHandle, &IoStatus, &SizeInfo, sizeof(SizeInfo), FileFsSizeInformation);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((SizeInfo.BytesPerSector == 0) || (SizeInfo.BytesPerSector > PAGE_SIZE) || (SizeInfo.SectorsPerAllocationUnit == 0))
        return STATUS_INVALID_DEVICE_STATE;

    Status = ObReferenceObjectByHandle(KdpBootLogHandle, 0, IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    Vpb = FileObject->Vpb;
    if ((Vpb == NULL) && (FileObject->DeviceObject != NULL))
        Vpb = FileObject->DeviceObject->Vpb;
    Device = (Vpb != NULL) ? Vpb->RealDevice : NULL;
    ObDereferenceObject(FileObject);
    if (Device == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_PARTITION_INFO_EX, Device, NULL, 0, &PartitionInfo, sizeof(PartitionInfo), FALSE, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(Device, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (!NT_SUCCESS(Status))
        return Status;

    KdpBootLogCrashBuffer = MmAllocateNonCachedMemory(KDP_BOOT_LOG_RESERVE_SIZE + PAGE_SIZE);
    if (KdpBootLogCrashBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KdpBootLogPartitionOffset = PartitionInfo.StartingOffset;
    KdpBootLogSectorSize = SizeInfo.BytesPerSector;
    KdpBootLogClusterSize = SizeInfo.BytesPerSector * SizeInfo.SectorsPerAllocationUnit;
    KdpBootLogDevice = Device;

    RtlZeroMemory(&KdpBootLogStorage, sizeof(KdpBootLogStorage));
    KdpBootLogStorage.BytesPerSector = KdpBootLogSectorSize;
    KeClearEvent(&Event);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_REACTOS_STORAGE_GET_DUMP_INTERFACE, Device, &KdpBootLogStorage, sizeof(KdpBootLogStorage), &KdpBootLogStorage, sizeof(KdpBootLogStorage), FALSE, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_SUCCESS;
    Status = IoCallDriver(Device, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    KdpBootLogStorageReady = NT_SUCCESS(Status) &&
                             (KdpBootLogStorage.Version == ROS_STORAGE_DUMP_INTERFACE_VERSION) &&
                             (KdpBootLogStorage.Size >= sizeof(KdpBootLogStorage)) &&
                             (KdpBootLogStorage.Context != NULL) &&
                             (KdpBootLogStorage.BytesPerSector == KdpBootLogSectorSize) &&
                             (KdpBootLogStorage.MaximumTransferLength >= KdpBootLogSectorSize) &&
                             (KdpBootLogStorage.Prepare != NULL) &&
                             (KdpBootLogStorage.WriteRoutine != NULL) &&
                             (KdpBootLogStorage.Flush != NULL);
    if (!KdpBootLogStorageReady)
        DPRINT1("KD: Boot log has no crash-time storage path of its own (0x%08lx)\n", Status);
    return STATUS_SUCCESS;
}

static NTSTATUS KdpBootLogTranslate(_In_ PRETRIEVAL_POINTERS_BUFFER Extents, _In_ ULONG64 FileOffset, _In_ ULONG Length, _Out_ PULONG64 DiskByteOffset, _Out_ PULONG ContiguousLength)
{
    ULONG64 Vcn;
    ULONG64 OffsetInCluster;
    ULONG64 RunStartVcn;
    ULONG64 RunEndVcn;
    ULONG Index;

    Vcn = FileOffset / KdpBootLogClusterSize;
    OffsetInCluster = FileOffset % KdpBootLogClusterSize;
    RunStartVcn = Extents->StartingVcn.QuadPart;
    for (Index = 0; Index < Extents->ExtentCount; Index++)
    {
        RunEndVcn = Extents->Extents[Index].NextVcn.QuadPart;
        if (Vcn < RunEndVcn)
        {
            if (Extents->Extents[Index].Lcn.QuadPart < 0)
                return STATUS_FILE_CORRUPT_ERROR;

            *DiskByteOffset = KdpBootLogPartitionOffset.QuadPart + ((ULONG64)Extents->Extents[Index].Lcn.QuadPart + Vcn - RunStartVcn) * KdpBootLogClusterSize + OffsetInCluster;
            *ContiguousLength = (ULONG)min((ULONG64)Length, (RunEndVcn - Vcn) * KdpBootLogClusterSize - OffsetInCluster);
            return STATUS_SUCCESS;
        }
        RunStartVcn = RunEndVcn;
    }

    return STATUS_END_OF_FILE;
}

NTSTATUS NTAPI KdpWriteBootLogAtCrash(_In_opt_ PROS_STORAGE_DUMP_INTERFACE Storage, _In_opt_ PFILE_OBJECT DumpFileObject)
{
    PKDP_BOOT_LOG_STATE State;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG64 DiskByteOffset;
    ULONG64 SectorStart;
    ULONG64 Position;
    ULONG64 Lost;
    PVPB Vpb;
    ULONG Prefix;
    ULONG Length;
    ULONG Total;
    ULONG Done;
    ULONG Chunk;
    NTSTATUS Status;

    State = KdpBootLogPublished;
    if (State == NULL)
        return STATUS_NOT_FOUND;

    if (Storage == NULL)
    {
        if (!KdpBootLogStorageReady)
            return STATUS_NOT_SUPPORTED;
        Storage = &KdpBootLogStorage;
        if (!KdpBootLogStoragePrepared)
        {
            Status = Storage->Prepare(Storage->Context);
            if (!NT_SUCCESS(Status))
                return Status;
            KdpBootLogStoragePrepared = TRUE;
        }
    }
    else
    {
        if (DumpFileObject == NULL)
            return STATUS_NOT_FOUND;
        Vpb = DumpFileObject->Vpb;
        if ((Vpb == NULL) && (DumpFileObject->DeviceObject != NULL))
            Vpb = DumpFileObject->DeviceObject->Vpb;
        if ((Vpb == NULL) || (Vpb->RealDevice != KdpBootLogDevice) || (Storage->BytesPerSector != KdpBootLogSectorSize))
            return STATUS_NOT_SUPPORTED;
    }
    if (State->ReserveEnd <= State->Written)
        return STATUS_DISK_FULL;

    Prefix = (ULONG)(State->Written % KdpBootLogSectorSize);
    SectorStart = State->Written - Prefix;
    RtlCopyMemory(KdpBootLogCrashBuffer, State->Tail, Prefix);

    Position = State->RingPosition;
    Length = KdpCopyPrintBufferFrom(&Position, (PCHAR)KdpBootLogCrashBuffer + Prefix, (ULONG)min(State->ReserveEnd - State->Written, KDP_BOOT_LOG_RESERVE_SIZE), &Lost);
    Total = ALIGN_UP_BY(Prefix + Length, KdpBootLogSectorSize);
    RtlZeroMemory(KdpBootLogCrashBuffer + Prefix + Length, Total - Prefix - Length);

    for (Done = 0; Done < Total; Done += Chunk)
    {
        Status = KdpBootLogTranslate(State->Extents, SectorStart + Done, Total - Done, &DiskByteOffset, &Chunk);
        if (!NT_SUCCESS(Status))
            return Status;

        Chunk = min(Chunk, PAGE_SIZE - BYTE_OFFSET(KdpBootLogCrashBuffer + Done));
        Chunk = ALIGN_DOWN_BY(min(Chunk, Storage->MaximumTransferLength), KdpBootLogSectorSize);
        if (Chunk == 0)
            return STATUS_INVALID_DEVICE_STATE;

        PhysicalAddress = MmGetPhysicalAddress(KdpBootLogCrashBuffer + Done);
        Status = Storage->WriteRoutine(Storage->Context, DiskByteOffset, PhysicalAddress, Chunk);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return Storage->Flush(Storage->Context);
}

VOID NTAPI KdShutdownBootLog(VOID)
{
    LARGE_INTEGER Timeout;
    PAGED_CODE();

    if (KdpBootLogHandle == NULL)
        return;

    KdpBootLogStopping = TRUE;
    KeSetEvent(&KdpBootLogWake, IO_NO_INCREMENT, FALSE);
    Timeout.QuadPart = -10000LL * KDP_BOOT_LOG_STOP_TIMEOUT_MS;
    KeWaitForSingleObject(&KdpBootLogStopped, Executive, KernelMode, FALSE, &Timeout);
}

VOID NTAPI KdInitializeBootLog(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    ANSI_STRING AnsiName;
    UNICODE_STRING FileName;
    IO_STATUS_BLOCK IoStatus;
    HANDLE ThreadHandle;
    KPRIORITY Priority;
    NTSTATUS Status;
    PAGED_CODE();

    RtlStringCbPrintfA(KdpBootLogName, sizeof(KdpBootLogName), "\\ArcName\\%s\\lastboot.log", LoaderBlock->ArcBootDeviceName);
    RtlInitAnsiString(&AnsiName, KdpBootLogName);
    Status = RtlAnsiStringToUnicodeString(&FileName, &AnsiName, TRUE);
    if (!NT_SUCCESS(Status))
        return;

    InitializeObjectAttributes(&ObjectAttributes, &FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateFile(&KdpBootLogHandle, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OVERWRITE_IF, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_SEQUENTIAL_ONLY | FILE_WRITE_THROUGH, NULL, 0);
    RtlFreeUnicodeString(&FileName);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("KD: Boot log %s not opened (0x%08lx)\n", KdpBootLogName, Status);
        KdpBootLogHandle = NULL;
        return;
    }

    Status = KdpBootLogQueryDevice();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("KD: Boot log %s is not saved at a crash (0x%08lx)\n", KdpBootLogName, Status);
        KdpBootLogDevice = NULL;
    }

    KeInitializeEvent(&KdpBootLogWake, SynchronizationEvent, FALSE);
    KeInitializeEvent(&KdpBootLogStopped, NotificationEvent, FALSE);
    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, KdpBootLogThread, NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("KD: Boot log writer not started (0x%08lx)\n", Status);
        ZwClose(KdpBootLogHandle);
        KdpBootLogHandle = NULL;
        return;
    }

    Priority = HIGH_PRIORITY;
    ZwSetInformationThread(ThreadHandle, ThreadPriority, &Priority, sizeof(Priority));
    ZwClose(ThreadHandle);
}
