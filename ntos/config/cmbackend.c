/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The memory, files and locks the hive library borrows from the kernel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include "cmpriv.h"

/* PRIVATE FUNCTIONS **********************************************************/

static
PEX_PUSH_LOCK
CmpHiveWriterLock(
    _In_ PHHIVE Hive)
{
    return (PEX_PUSH_LOCK)&Hive->WriterLock.Reserved;
}

static
PEX_PUSH_LOCK
CmpHiveFlusherLock(
    _In_ PHHIVE Hive)
{
    return (PEX_PUSH_LOCK)&Hive->FlusherLock.Reserved;
}

static
HANDLE
CmpHiveFileHandle(
    _In_ PHHIVE Hive,
    _In_ ULONG FileType)
{
    PCMHIVE CmHive = CONTAINING_RECORD(Hive, CMHIVE, Hive);

    if (FileType >= HFILE_TYPE_MAX) return NULL;
    return CmHive->FileHandles[FileType];
}

/* PUBLIC FUNCTIONS ***********************************************************/

PVOID
NTAPI
CmpAllocate(
    _In_ SIZE_T Size,
    _In_ BOOLEAN Paged,
    _In_ ULONG Tag)
{
    return ExAllocatePoolWithTag(Paged ? PagedPool : NonPagedPool, Size, Tag);
}

VOID
NTAPI
CmpFree(
    _In_ PVOID Ptr,
    _In_ ULONG Quota)
{
    UNREFERENCED_PARAMETER(Quota);
    ExFreePool(Ptr);
}

BOOLEAN
NTAPI
CmpFileRead(
    _In_ PHHIVE RegistryHive,
    _In_ ULONG FileType,
    _Inout_ PULONG FileOffset,
    _Out_ PVOID Buffer,
    _In_ SIZE_T BufferLength)
{
    HANDLE Handle = CmpHiveFileHandle(RegistryHive, FileType);
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    NTSTATUS Status;

    if (Handle == NULL) return FALSE;

    Offset.QuadPart = *FileOffset;
    Status = ZwReadFile(Handle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatus,
                        Buffer,
                        (ULONG)BufferLength,
                        &Offset,
                        NULL);
    if (!NT_SUCCESS(Status)) return FALSE;

    *FileOffset += (ULONG)IoStatus.Information;
    return IoStatus.Information == BufferLength;
}

BOOLEAN
NTAPI
CmpFileWrite(
    _In_ PHHIVE RegistryHive,
    _In_ ULONG FileType,
    _Inout_ PULONG FileOffset,
    _In_ PVOID Buffer,
    _In_ SIZE_T BufferLength)
{
    HANDLE Handle = CmpHiveFileHandle(RegistryHive, FileType);
    IO_STATUS_BLOCK IoStatus;
    LARGE_INTEGER Offset;
    NTSTATUS Status;

    if (CmpNoWrite) return TRUE;
    if (Handle == NULL) return FALSE;

    Offset.QuadPart = *FileOffset;
    Status = ZwWriteFile(Handle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatus,
                         Buffer,
                         (ULONG)BufferLength,
                         &Offset,
                         NULL);
    if (!NT_SUCCESS(Status)) return FALSE;

    *FileOffset += (ULONG)IoStatus.Information;
    return IoStatus.Information == BufferLength;
}

BOOLEAN
NTAPI
CmpFileSetSize(
    _In_ PHHIVE RegistryHive,
    _In_ ULONG FileType,
    _In_ ULONG FileSize,
    _In_ ULONG OldFileSize)
{
    HANDLE Handle = CmpHiveFileHandle(RegistryHive, FileType);
    FILE_END_OF_FILE_INFORMATION EndOfFile;
    FILE_ALLOCATION_INFORMATION Allocation;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    if (CmpNoWrite) return TRUE;
    if (Handle == NULL) return FALSE;

    if (FileSize > OldFileSize)
    {
        Allocation.AllocationSize.QuadPart = FileSize;
        Status = ZwSetInformationFile(Handle,
                                      &IoStatus,
                                      &Allocation,
                                      sizeof(Allocation),
                                      FileAllocationInformation);
        if (!NT_SUCCESS(Status)) return FALSE;
    }

    EndOfFile.EndOfFile.QuadPart = FileSize;
    Status = ZwSetInformationFile(Handle,
                                  &IoStatus,
                                  &EndOfFile,
                                  sizeof(EndOfFile),
                                  FileEndOfFileInformation);
    if (!NT_SUCCESS(Status)) return FALSE;

    if (FileSize < OldFileSize)
    {
        Allocation.AllocationSize.QuadPart = FileSize;
        Status = ZwSetInformationFile(Handle,
                                      &IoStatus,
                                      &Allocation,
                                      sizeof(Allocation),
                                      FileAllocationInformation);
        if (!NT_SUCCESS(Status)) return FALSE;
    }

    return TRUE;
}

BOOLEAN
NTAPI
CmpFileFlush(
    _In_ PHHIVE RegistryHive,
    _In_ ULONG FileType,
    _Inout_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length)
{
    HANDLE Handle = CmpHiveFileHandle(RegistryHive, FileType);
    IO_STATUS_BLOCK IoStatus;

    UNREFERENCED_PARAMETER(FileOffset);
    UNREFERENCED_PARAMETER(Length);

    if (CmpNoWrite) return TRUE;
    if (Handle == NULL) return FALSE;

    return NT_SUCCESS(ZwFlushBuffersFile(Handle, &IoStatus));
}

NTSTATUS
NTAPI
CmpCreateEvent(
    _In_ EVENT_TYPE EventType,
    _Out_ PHANDLE EventHandle,
    _Out_ PKEVENT *Event)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateEvent(EventHandle,
                           EVENT_ALL_ACCESS,
                           &ObjectAttributes,
                           EventType,
                           FALSE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(*EventHandle,
                                       EVENT_ALL_ACCESS,
                                       ExEventObjectType,
                                       KernelMode,
                                       (PVOID*)Event,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(*EventHandle);
        *EventHandle = NULL;
        *Event = NULL;
    }

    return Status;
}

VOID
CMAPI
HvLockHiveWriter(
    _In_ PHHIVE RegistryHive)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(CmpHiveWriterLock(RegistryHive));
}

VOID
CMAPI
HvUnlockHiveWriter(
    _In_ PHHIVE RegistryHive)
{
    ExReleasePushLockExclusive(CmpHiveWriterLock(RegistryHive));
    KeLeaveCriticalRegion();
}

/* The thread still loading a hive already owns it, so it reads without a lock */
BOOLEAN
CMAPI
HvLockHiveReader(
    _In_ PHHIVE RegistryHive)
{
    PCMHIVE CmHive = CONTAINING_RECORD(RegistryHive, CMHIVE, Hive);

    if (CmHive->CreatorOwner == KeGetCurrentThread()) return FALSE;

    KeEnterCriticalRegion();
    ExAcquirePushLockShared(CmpHiveWriterLock(RegistryHive));
    return TRUE;
}

VOID
CMAPI
HvUnlockHiveReader(
    _In_ PHHIVE RegistryHive,
    _In_ BOOLEAN LockAcquired)
{
    if (!LockAcquired) return;

    ExReleasePushLockShared(CmpHiveWriterLock(RegistryHive));
    KeLeaveCriticalRegion();
}

VOID
NTAPI
CmpLockHiveSecurity(
    _In_ PHHIVE Hive)
{
    PCMHIVE CmHive = CONTAINING_RECORD(Hive, CMHIVE, Hive);

    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&CmHive->SecurityLock);
}

VOID
NTAPI
CmpUnlockHiveSecurity(
    _In_ PHHIVE Hive)
{
    PCMHIVE CmHive = CONTAINING_RECORD(Hive, CMHIVE, Hive);

    ExReleasePushLockExclusive(&CmHive->SecurityLock);
    KeLeaveCriticalRegion();
}

VOID
NTAPI
CmpLockHiveFlusherExclusive(
    _In_ PCMHIVE Hive)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(CmpHiveFlusherLock(&Hive->Hive));
}

VOID
NTAPI
CmpLockHiveFlusherShared(
    _In_ PCMHIVE Hive)
{
    KeEnterCriticalRegion();
    ExAcquirePushLockShared(CmpHiveFlusherLock(&Hive->Hive));
}

VOID
NTAPI
CmpUnlockHiveFlusher(
    _In_ PCMHIVE Hive)
{
    ExReleasePushLock(CmpHiveFlusherLock(&Hive->Hive));
    KeLeaveCriticalRegion();
}
