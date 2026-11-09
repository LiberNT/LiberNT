/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Saying whether a reparse point buffer holds what it claims
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

static
NTSTATUS
FsRtlpValidateNamePair(
    _In_ USHORT PathBufferLength,
    _In_ USHORT SubstituteNameOffset,
    _In_ USHORT SubstituteNameLength,
    _In_ USHORT PrintNameOffset,
    _In_ USHORT PrintNameLength)
{
    if (FlagOn(SubstituteNameOffset, 1) || FlagOn(SubstituteNameLength, 1) ||
        FlagOn(PrintNameOffset, 1) || FlagOn(PrintNameLength, 1))
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (SubstituteNameLength == 0) return STATUS_IO_REPARSE_DATA_INVALID;

    if (SubstituteNameOffset > PathBufferLength)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (SubstituteNameLength > PathBufferLength - SubstituteNameOffset)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (PrintNameOffset > PathBufferLength)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (PrintNameLength > PathBufferLength - PrintNameOffset)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
FsRtlpValidateSymbolicLink(
    _In_ PREPARSE_DATA_BUFFER Buffer)
{
    ULONG HeaderBytes;
    USHORT PathBufferLength;

    HeaderBytes = FIELD_OFFSET(REPARSE_DATA_BUFFER,
                               SymbolicLinkReparseBuffer.PathBuffer) -
                  REPARSE_DATA_BUFFER_HEADER_SIZE;

    if (Buffer->ReparseDataLength < HeaderBytes)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (FlagOn(Buffer->SymbolicLinkReparseBuffer.Flags,
               ~(ULONG)SYMLINK_FLAG_RELATIVE))
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    PathBufferLength = (USHORT)(Buffer->ReparseDataLength - HeaderBytes);

    return FsRtlpValidateNamePair(
               PathBufferLength,
               Buffer->SymbolicLinkReparseBuffer.SubstituteNameOffset,
               Buffer->SymbolicLinkReparseBuffer.SubstituteNameLength,
               Buffer->SymbolicLinkReparseBuffer.PrintNameOffset,
               Buffer->SymbolicLinkReparseBuffer.PrintNameLength);
}

static
NTSTATUS
FsRtlpValidateMountPoint(
    _In_ PREPARSE_DATA_BUFFER Buffer)
{
    ULONG HeaderBytes;
    USHORT PathBufferLength;

    HeaderBytes = FIELD_OFFSET(REPARSE_DATA_BUFFER,
                               MountPointReparseBuffer.PathBuffer) -
                  REPARSE_DATA_BUFFER_HEADER_SIZE;

    if (Buffer->ReparseDataLength < HeaderBytes)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    PathBufferLength = (USHORT)(Buffer->ReparseDataLength - HeaderBytes);

    return FsRtlpValidateNamePair(
               PathBufferLength,
               Buffer->MountPointReparseBuffer.SubstituteNameOffset,
               Buffer->MountPointReparseBuffer.SubstituteNameLength,
               Buffer->MountPointReparseBuffer.PrintNameOffset,
               Buffer->MountPointReparseBuffer.PrintNameLength);
}

static
NTSTATUS
FsRtlpValidateGuidReparse(
    _In_ ULONG BufferLength,
    _In_ PREPARSE_DATA_BUFFER Buffer)
{
    PREPARSE_GUID_DATA_BUFFER Guid = (PREPARSE_GUID_DATA_BUFFER)Buffer;

    if (BufferLength < REPARSE_GUID_DATA_BUFFER_HEADER_SIZE)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (BufferLength != REPARSE_GUID_DATA_BUFFER_HEADER_SIZE +
                            (ULONG)Guid->ReparseDataLength)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
FsRtlValidateReparsePointBuffer(
    _In_ ULONG BufferLength,
    _In_ PREPARSE_DATA_BUFFER ReparseBuffer)
{
    ULONG Tag;

    PAGED_CODE();

    if (ReparseBuffer == NULL) return STATUS_IO_REPARSE_DATA_INVALID;

    if (BufferLength < REPARSE_DATA_BUFFER_HEADER_SIZE)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (BufferLength > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    Tag = ReparseBuffer->ReparseTag;

    if (!IsReparseTagValid(Tag)) return STATUS_IO_REPARSE_TAG_INVALID;

    if (!IsReparseTagMicrosoft(Tag))
    {
        return FsRtlpValidateGuidReparse(BufferLength, ReparseBuffer);
    }

    if (BufferLength != REPARSE_DATA_BUFFER_HEADER_SIZE +
                            (ULONG)ReparseBuffer->ReparseDataLength)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (ReparseBuffer->Reserved != 0) return STATUS_IO_REPARSE_DATA_INVALID;

    switch (Tag)
    {
        case IO_REPARSE_TAG_SYMLINK:
            return FsRtlpValidateSymbolicLink(ReparseBuffer);

        case IO_REPARSE_TAG_MOUNT_POINT:
            return FsRtlpValidateMountPoint(ReparseBuffer);

        default:
            return STATUS_SUCCESS;
    }
}

/* EOF */
