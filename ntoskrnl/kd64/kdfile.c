/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver replacement through the kernel debugger (.kdfiles)
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <hi@ostapbrehin.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KDP_FILE_IO_DATA_MAXIMUM (PACKET_MAX_SIZE - sizeof(DBGKD_FILE_IO))

static
BOOLEAN
KdpSendFileIo(
    _Inout_ PDBGKD_FILE_IO FileIo,
    _In_opt_ PSTRING RequestData,
    _Inout_opt_ PSTRING ResponseData)
{
    STRING Header, Data;
    DBGKD_FILE_IO Request = *FileIo;
    ULONG Length;
    KDSTATUS Status;
    BOOLEAN Enable;

    Enable = KdEnterDebugger(NULL, NULL);

    do
    {
        *FileIo = Request;
        Header.Length = sizeof(DBGKD_FILE_IO);
        Header.Buffer = (PCHAR)FileIo;
        KdSendPacket(PACKET_TYPE_KD_FILE_IO, &Header, RequestData, &KdpContext);

        Header.MaximumLength = sizeof(DBGKD_FILE_IO);
        if (ResponseData)
        {
            Data = *ResponseData;
        }
        else
        {
            Data.Buffer = KdpMessageBuffer;
            Data.MaximumLength = sizeof(KdpMessageBuffer);
        }
        Data.Length = 0;

        do
        {
            Status = KdReceivePacket(PACKET_TYPE_KD_FILE_IO,
                                     &Header,
                                     &Data,
                                     &Length,
                                     &KdpContext);
        } while ((Status == KdPacketReceived) && (FileIo->ApiNumber != Request.ApiNumber));
    } while (Status == KdPacketNeedsResend);

    KdExitDebugger(Enable);

    if (Status != KdPacketReceived)
        return FALSE;

    if (ResponseData)
        ResponseData->Length = (USHORT)min(Length, ResponseData->MaximumLength);

    return TRUE;
}

NTSTATUS
NTAPI
KdPullRemoteFile(
    _In_ PUNICODE_STRING FileName)
{
    DBGKD_FILE_IO FileIo;
    STRING Data;
    PCHAR Buffer, FileData = NULL;
    ULONG64 Handle, FileSize, Offset;
    LARGE_INTEGER AllocationSize;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    NTSTATUS Status;

    PAGED_CODE();

    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
        return STATUS_DEBUGGER_INACTIVE;

    if ((ULONG)FileName->Length + sizeof(UNICODE_NULL) > KDP_FILE_IO_DATA_MAXIMUM)
        return STATUS_NAME_TOO_LONG;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, KDP_FILE_IO_DATA_MAXIMUM, TAG_KDBG);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, FileName->Buffer, FileName->Length);
    *(PWCHAR)(Buffer + FileName->Length) = UNICODE_NULL;
    Data.Buffer = Buffer;
    Data.Length = FileName->Length + sizeof(UNICODE_NULL);
    Data.MaximumLength = KDP_FILE_IO_DATA_MAXIMUM;

    RtlZeroMemory(&FileIo, sizeof(FileIo));
    FileIo.ApiNumber = DbgKdCreateFileApi;
    FileIo.u.CreateFile.DesiredAccess = GENERIC_READ | FILE_GENERIC_READ;
    FileIo.u.CreateFile.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    FileIo.u.CreateFile.ShareAccess = FILE_SHARE_READ;
    FileIo.u.CreateFile.CreateDisposition = FILE_OPEN;
    if (!KdpSendFileIo(&FileIo, &Data, NULL))
    {
        Status = STATUS_IO_TIMEOUT;
        goto Exit;
    }

    Status = (NTSTATUS)FileIo.Status;
    if (!NT_SUCCESS(Status))
        goto Exit;

    Handle = FileIo.u.CreateFile.Handle;
    FileSize = FileIo.u.CreateFile.Length;

    DPRINT1("KD: Replacing %wZ with the debugger's copy (%I64u bytes)\n", FileName, FileSize);

    if (FileSize > MAXULONG)
        Status = STATUS_FILE_TOO_LARGE;
    else if (FileSize && !(FileData = ExAllocatePoolWithTag(PagedPool, (SIZE_T)FileSize, TAG_KDBG)))
        Status = STATUS_INSUFFICIENT_RESOURCES;

    for (Offset = 0; NT_SUCCESS(Status) && (Offset < FileSize); Offset += Data.Length)
    {
        RtlZeroMemory(&FileIo, sizeof(FileIo));
        FileIo.ApiNumber = DbgKdReadFileApi;
        FileIo.u.ReadFile.Handle = Handle;
        FileIo.u.ReadFile.Offset = Offset;
        FileIo.u.ReadFile.Length = (ULONG)min(FileSize - Offset, KDP_FILE_IO_DATA_MAXIMUM);
        Data.MaximumLength = (USHORT)FileIo.u.ReadFile.Length;
        if (!KdpSendFileIo(&FileIo, NULL, &Data))
        {
            Status = STATUS_IO_TIMEOUT;
            break;
        }

        Status = (NTSTATUS)FileIo.Status;
        if (NT_SUCCESS(Status))
            RtlCopyMemory(FileData + Offset, Buffer, Data.Length);
    }

    RtlZeroMemory(&FileIo, sizeof(FileIo));
    FileIo.ApiNumber = DbgKdCloseFileApi;
    FileIo.u.CloseFile.Handle = Handle;
    KdpSendFileIo(&FileIo, NULL, NULL);

    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes,
                                   FileName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);
        AllocationSize.QuadPart = (LONGLONG)FileSize;
        Status = ZwCreateFile(&FileHandle,
                              FILE_GENERIC_WRITE,
                              &ObjectAttributes,
                              &IoStatusBlock,
                              &AllocationSize,
                              FILE_ATTRIBUTE_NORMAL,
                              0,
                              FILE_OVERWRITE_IF,
                              FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                              NULL,
                              0);
        if (NT_SUCCESS(Status))
        {
            if (FileData)
            {
                Status = ZwWriteFile(FileHandle,
                                     NULL,
                                     NULL,
                                     NULL,
                                     &IoStatusBlock,
                                     FileData,
                                     (ULONG)FileSize,
                                     NULL,
                                     NULL);
            }
            ZwClose(FileHandle);
        }
    }

    if (!NT_SUCCESS(Status))
        DPRINT1("KD: Replacing %wZ failed with status 0x%lx\n", FileName, Status);

    if (FileData)
        ExFreePoolWithTag(FileData, TAG_KDBG);

Exit:
    ExFreePoolWithTag(Buffer, TAG_KDBG);
    return Status;
}
