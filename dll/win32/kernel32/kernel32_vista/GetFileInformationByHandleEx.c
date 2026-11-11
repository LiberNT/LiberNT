
#include "k32_vista.h"

#include <ndk/rtlfuncs.h>
#include <ndk/iofuncs.h>

#define NDEBUG
#include <debug.h>

#undef FIXME
#define FIXME DPRINT1

/* Taken from Wine kernel32/file.c */

/***********************************************************************
 *	GetFileInformationByHandleEx   (kernelbase.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH GetFileInformationByHandleEx(HANDLE handle, FILE_INFO_BY_HANDLE_CLASS class,
    LPVOID info, DWORD size)
{
    NTSTATUS status;
    IO_STATUS_BLOCK io;

    switch (class)
    {
    case FileRemoteProtocolInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileRemoteProtocolInformation);
        break;

    case FileCaseSensitiveInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileCaseSensitiveInformation);
        break;

    case FileNormalizedNameInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileNormalizedNameInformation);
        break;

    case FileStorageInfo:
        status = NtQueryVolumeInformationFile(handle, &io, info, size, FileFsSectorSizeInformation);
        break;

    case FileStreamInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileStreamInformation);
        if (NT_SUCCESS(status) && io.Information == 0)
            status = STATUS_END_OF_FILE;
        break;

    case FileCompressionInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileCompressionInformation);
        break;

    case FileAlignmentInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileAlignmentInformation);
        break;

    case FileAttributeTagInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileAttributeTagInformation);
        break;

    case FileBasicInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileBasicInformation);
        break;

    case FileStandardInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileStandardInformation);
        break;

    case FileNameInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileNameInformation);
        break;

    case FileIdInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileIdInformation);
        break;

    case FileIdBothDirectoryRestartInfo:
    case FileIdBothDirectoryInfo:
        status = NtQueryDirectoryFile(handle, NULL, NULL, NULL, &io, info, size,
            FileIdBothDirectoryInformation, FALSE, NULL,
            (class == FileIdBothDirectoryRestartInfo));
        break;

    case FileFullDirectoryInfo:
    case FileFullDirectoryRestartInfo:
        status = NtQueryDirectoryFile(handle, NULL, NULL, NULL, &io, info, size,
            FileFullDirectoryInformation, FALSE, NULL,
            (class == FileFullDirectoryRestartInfo));
        break;

    case FileIdExtdDirectoryInfo:
    case FileIdExtdDirectoryRestartInfo:
        status = NtQueryDirectoryFile(handle, NULL, NULL, NULL, &io, info, size,
            FileIdExtdDirectoryInformation, FALSE, NULL,
            (class == FileIdExtdDirectoryRestartInfo));
        break;

    case FileRenameInfo:
    case FileDispositionInfo:
    case FileAllocationInfo:
    case FileIoPriorityHintInfo:
    case FileEndOfFileInfo:
    case FileDispositionInfoEx:
    case FileRenameInfoEx:
    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

#ifdef __REACTOS__
    if (!NT_SUCCESS(status))
    {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }

    return TRUE;
#else
    return set_ntstatus(status);
#endif
}
