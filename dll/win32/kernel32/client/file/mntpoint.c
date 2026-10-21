/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            dll/win32/kernel32/client/file/mntpoint.c
 * PURPOSE:         File volume mount point functions
 * PROGRAMMER:      Pierre Schweitzer (pierre@reactos.org)
 */

#include <k32.h>
#define NDEBUG
#include <debug.h>

/*
 * @implemented
 */
static BOOL
GetVolumeNameForRoot(IN LPCWSTR lpszRootPath,
                     OUT LPWSTR lpszVolumeName,
                     IN DWORD cchBufferLength)
{
    BOOL Ret;
    NTSTATUS Status;
    PWSTR FoundVolume;
    DWORD BytesReturned;
    UNICODE_STRING NtPathName;
    IO_STATUS_BLOCK IoStatusBlock;
    PMOUNTMGR_MOUNT_POINT MountPoint;
    ULONG CurrentMntPt, FoundVolumeLen;
    PMOUNTMGR_MOUNT_POINTS MountPoints;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE VolumeHandle, MountMgrHandle;
    struct
    {
        MOUNTDEV_NAME;
        WCHAR Buffer[MAX_PATH];
    } MountDevName;

    /* It makes no sense on a non-local drive */
    if (GetDriveTypeW(lpszRootPath) == DRIVE_REMOTE)
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }

    /* Get the NT path */
    if (!RtlDosPathNameToNtPathName_U(lpszRootPath, &NtPathName, NULL, NULL))
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }

    /* If it's a root path - likely - drop backslash to open volume */
    if (NtPathName.Buffer[(NtPathName.Length / sizeof(WCHAR)) - 1] == L'\\')
    {
        NtPathName.Buffer[(NtPathName.Length / sizeof(WCHAR)) - 1] = UNICODE_NULL;
        NtPathName.Length -= sizeof(WCHAR);
    }

    /* If that's a DOS volume, upper case the letter */
    if (NtPathName.Length >= 2 * sizeof(WCHAR))
    {
        if (NtPathName.Buffer[(NtPathName.Length / sizeof(WCHAR)) - 1] == L':')
        {
            NtPathName.Buffer[(NtPathName.Length / sizeof(WCHAR)) - 2] = towupper(NtPathName.Buffer[(NtPathName.Length / sizeof(WCHAR)) - 2]);
        }
    }

    /* Attempt to open the volume */
    InitializeObjectAttributes(&ObjectAttributes, &NtPathName,
                               OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&VolumeHandle, SYNCHRONIZE | FILE_READ_ATTRIBUTES,
                        &ObjectAttributes, &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_ALERT);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);
        BaseSetLastNTError(Status);
        return FALSE;
    }

    /* Query the device name - that's what we'll translate */
    if (!DeviceIoControl(VolumeHandle, IOCTL_MOUNTDEV_QUERY_DEVICE_NAME, NULL,
                         0, &MountDevName, sizeof(MountDevName), &BytesReturned,
                         NULL))
    {
        NtClose(VolumeHandle);
        RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);
        return FALSE;
    }

    /* No longer need the volume */
    NtClose(VolumeHandle);
    RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);

    /* We'll keep the device name for later usage */
    NtPathName.Length = MountDevName.NameLength;
    NtPathName.MaximumLength = MountDevName.NameLength + sizeof(UNICODE_NULL);
    NtPathName.Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, NtPathName.MaximumLength);
    if (NtPathName.Buffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    RtlCopyMemory(NtPathName.Buffer, MountDevName.Name, NtPathName.Length);
    NtPathName.Buffer[NtPathName.Length / sizeof(WCHAR)] = UNICODE_NULL;

    /* Allocate the structure for querying the mount mgr */
    MountPoint = RtlAllocateHeap(RtlGetProcessHeap(), 0,
                                 NtPathName.Length + sizeof(MOUNTMGR_MOUNT_POINT));
    if (MountPoint == NULL)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    /* 0 everything, we provide a device name */
    RtlZeroMemory(MountPoint, sizeof(MOUNTMGR_MOUNT_POINT));
    MountPoint->DeviceNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
    MountPoint->DeviceNameLength = NtPathName.Length;
    RtlCopyMemory((PVOID)((ULONG_PTR)MountPoint + sizeof(MOUNTMGR_MOUNT_POINT)), NtPathName.Buffer, NtPathName.Length);

    /* Allocate a dummy output buffer to probe for size */
    MountPoints = RtlAllocateHeap(RtlGetProcessHeap(), 0, sizeof(MOUNTMGR_MOUNT_POINTS));
    if (MountPoints == NULL)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);
        RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoint);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    /* Open a handle to the mount manager */
    MountMgrHandle = CreateFileW(MOUNTMGR_DOS_DEVICE_NAME, 0,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                 INVALID_HANDLE_VALUE);
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);
        RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoints);
        RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoint);
        return FALSE;
    }

    /* Query the names associated to our device name */
    Ret = DeviceIoControl(MountMgrHandle, IOCTL_MOUNTMGR_QUERY_POINTS,
                          MountPoint, NtPathName.Length + sizeof(MOUNTMGR_MOUNT_POINT),
                          MountPoints, sizeof(MOUNTMGR_MOUNT_POINTS), &BytesReturned,
                          NULL);
    /* As long as the buffer is too small, keep looping */
    while (!Ret && GetLastError() == ERROR_MORE_DATA)
    {
        ULONG BufferSize;

        /* Get the size we've to allocate */
        BufferSize = MountPoints->Size;
        /* Reallocate the buffer with enough room */
        RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoints);
        MountPoints = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferSize);
        if (MountPoints == NULL)
        {
            CloseHandle(MountMgrHandle);
            RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);
            RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoint);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        /* Reissue the request, it should work now! */
        Ret = DeviceIoControl(MountMgrHandle, IOCTL_MOUNTMGR_QUERY_POINTS,
                              MountPoint, NtPathName.Length + sizeof(MOUNTMGR_MOUNT_POINT),
                              MountPoints, BufferSize, &BytesReturned, NULL);
    }

    /* We're done, no longer need the mount manager */
    CloseHandle(MountMgrHandle);
    /* Nor our input buffer */
    RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoint);

    /* If the mount manager failed, just quit */
    if (!Ret)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);
        RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoints);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    CurrentMntPt = 0;
    /* If there were no associated mount points, we'll return the device name */
    if (MountPoints->NumberOfMountPoints == 0)
    {
        FoundVolume = NtPathName.Buffer;
        FoundVolumeLen = NtPathName.Length;
    }
    /* Otherwise, find one which is matching */
    else
    {
        for (; CurrentMntPt < MountPoints->NumberOfMountPoints; ++CurrentMntPt)
        {
            UNICODE_STRING SymbolicLink;

            /* Make a string of it, to easy the checks */
            SymbolicLink.Length = MountPoints->MountPoints[CurrentMntPt].SymbolicLinkNameLength;
            SymbolicLink.MaximumLength = SymbolicLink.Length;
            SymbolicLink.Buffer = (PVOID)((ULONG_PTR)MountPoints + MountPoints->MountPoints[CurrentMntPt].SymbolicLinkNameOffset);
            /* If that's a NT volume name (GUID form), keep it! */
            if (MOUNTMGR_IS_NT_VOLUME_NAME(&SymbolicLink))
            {
                FoundVolume = SymbolicLink.Buffer;
                FoundVolumeLen = SymbolicLink.Length;

                break;
            }
        }
    }

    /* We couldn't find anything matching, return an error */
    if (CurrentMntPt == MountPoints->NumberOfMountPoints)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);
        RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoints);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* We found a matching volume, have we enough memory to return it? */
    if (cchBufferLength * sizeof(WCHAR) < FoundVolumeLen + 2 * sizeof(WCHAR))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);
        RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoints);
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }

    /* Copy it back! */
    RtlCopyMemory(lpszVolumeName, FoundVolume, FoundVolumeLen);
    /* Make it compliant */
    lpszVolumeName[1] = L'\\';
    /* And transform it as root path */
    lpszVolumeName[FoundVolumeLen / sizeof(WCHAR)] = L'\\';
    lpszVolumeName[FoundVolumeLen / sizeof(WCHAR) + 1] = UNICODE_NULL;

    /* We're done! */
    RtlFreeHeap(RtlGetProcessHeap(), 0, NtPathName.Buffer);
    RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoints);
    return TRUE;
}

/*
 * @implemented
 */
BOOL
BasepGetVolumeNameFromReparsePoint(IN LPCWSTR lpszMountPoint,
                                   OUT LPWSTR lpszVolumeName,
                                   IN DWORD cchBufferLength,
                                   OUT LPBOOL IsAMountPoint)
{
    WCHAR Old;
    DWORD BytesReturned;
    HANDLE ReparseHandle;
    UNICODE_STRING SubstituteName;
    PREPARSE_DATA_BUFFER ReparseBuffer;

    /* Try to open the reparse point */
    ReparseHandle = CreateFileW(lpszMountPoint, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS | FILE_ATTRIBUTE_NORMAL,
                                INVALID_HANDLE_VALUE);
    /* It failed! */
    if (ReparseHandle == INVALID_HANDLE_VALUE)
    {
        /* Report it's not a mount point (it's not a reparse point) */
        if (IsAMountPoint != NULL)
        {
            *IsAMountPoint = FALSE;
        }

        /* And zero output */
        if (lpszVolumeName != NULL && cchBufferLength >= 1)
        {
            lpszVolumeName[0] = UNICODE_NULL;
        }

        return FALSE;
    }

    /* This is a mount point! */
    if (IsAMountPoint != NULL)
    {
        *IsAMountPoint = TRUE;
    }

    /* Prepare a buffer big enough to read its data */
    ReparseBuffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    if (ReparseBuffer == NULL)
    {
        CloseHandle(ReparseHandle);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);

        /* Zero output */
        if (lpszVolumeName != NULL && cchBufferLength >= 1)
        {
            lpszVolumeName[0] = UNICODE_NULL;
        }

        return FALSE;
    }

    /* Dump the reparse point data */
    if (!DeviceIoControl(ReparseHandle, FSCTL_GET_REPARSE_POINT, NULL, 0,
                         ReparseBuffer, MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &BytesReturned,
                         NULL))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, ReparseBuffer);
        CloseHandle(ReparseHandle);

        /* Zero output */
        if (lpszVolumeName != NULL && cchBufferLength >= 1)
        {
            lpszVolumeName[0] = UNICODE_NULL;
        }

        return FALSE;
    }

    /* We no longer need the reparse point */
    CloseHandle(ReparseHandle);

    /* We only handle mount points */
    if (ReparseBuffer->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, ReparseBuffer);

        /* Zero output */
        if (lpszVolumeName != NULL && cchBufferLength >= 1)
        {
            lpszVolumeName[0] = UNICODE_NULL;
        }

        return FALSE;
    }

    /* Do we have enough room for copying substitue name? */
    if ((ReparseBuffer->MountPointReparseBuffer.SubstituteNameLength + sizeof(UNICODE_NULL)) > cchBufferLength * sizeof(WCHAR))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, ReparseBuffer);
        SetLastError(ERROR_FILENAME_EXCED_RANGE);

        /* Zero output */
        if (lpszVolumeName != NULL && cchBufferLength >= 1)
        {
            lpszVolumeName[0] = UNICODE_NULL;
        }

        return FALSE;
    }

    /* Copy the link target */
    RtlCopyMemory(lpszVolumeName,
                  &ReparseBuffer->MountPointReparseBuffer.PathBuffer[ReparseBuffer->MountPointReparseBuffer.SubstituteNameOffset / sizeof(WCHAR)],
                  ReparseBuffer->MountPointReparseBuffer.SubstituteNameLength);
    /* Make it DOS valid */
    Old = lpszVolumeName[1];
    /* We want a root path */
    lpszVolumeName[1] = L'\\';
    /* And null terminate obviously */
    lpszVolumeName[ReparseBuffer->MountPointReparseBuffer.SubstituteNameLength / sizeof(WCHAR)] = UNICODE_NULL;

    /* Make it a string to easily check it */
    SubstituteName.Length = ReparseBuffer->MountPointReparseBuffer.SubstituteNameLength;
    SubstituteName.MaximumLength = SubstituteName.Length;
    SubstituteName.Buffer = lpszVolumeName;

    /* No longer need the data? */
    RtlFreeHeap(RtlGetProcessHeap(), 0, ReparseBuffer);

    /* Is that a dos volume name with backslash? */
    if (MOUNTMGR_IS_DOS_VOLUME_NAME_WB(&SubstituteName))
    {
        return TRUE;
    }

    /* No, so restore previous name and return to the caller */
    lpszVolumeName[1] = Old;
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}

/*
 * @implemented
 */
BOOL
BasepGetVolumeNameForVolumeMountPoint(IN LPCWSTR lpszMountPoint,
                                      OUT LPWSTR lpszVolumeName,
                                      IN DWORD cchBufferLength,
                                      OUT LPBOOL IsAMountPoint)
{
    BOOL Ret;
    UNICODE_STRING MountPoint;

    /* Assume it's a mount point (likely for non reparse points) */
    if (IsAMountPoint != NULL)
    {
        *IsAMountPoint = 1;
    }

    /* Make a string with the mount point name */
    RtlInitUnicodeString(&MountPoint, lpszMountPoint);
    /* Not a root path? */
    if (MountPoint.Buffer[(MountPoint.Length / sizeof(WCHAR)) - 1] != L'\\')
    {
        BaseSetLastNTError(STATUS_OBJECT_NAME_INVALID);
        /* Zero output */
        if (lpszVolumeName != NULL && cchBufferLength >= 1)
        {
            lpszVolumeName[0] = UNICODE_NULL;
        }

        return FALSE;
    }

    /* Does it look like <letter>:\? */
    if (MountPoint.Length == 3 * sizeof(WCHAR))
    {
        /* Try to get volume name for root path */
        Ret = GetVolumeNameForRoot(lpszMountPoint, lpszVolumeName, cchBufferLength);
        /* It failed? */
        if (!Ret)
        {
            /* If wasn't a drive letter, so maybe a reparse point? */
            if (MountPoint.Buffer[1] != ':')
            {
                Ret = BasepGetVolumeNameFromReparsePoint(lpszMountPoint, lpszVolumeName, cchBufferLength, IsAMountPoint);
            }
            /* It was, so zero output */
            else if (lpszVolumeName != NULL && cchBufferLength >= 1)
            {
                lpszVolumeName[0] = UNICODE_NULL;
            }
        }
    }
    else
    {
        /* Try to get volume name for root path */
        Ret = GetVolumeNameForRoot(lpszMountPoint, lpszVolumeName, cchBufferLength);
        /* It failed? */
        if (!Ret)
        {
            /* It was a DOS volume as UNC name, so fail and zero output */
            if (MountPoint.Length == 14 && MountPoint.Buffer[0] == '\\' && MountPoint.Buffer[1] == '\\' &&
                (MountPoint.Buffer[2] == '.' || MountPoint.Buffer[2] == '?') && MountPoint.Buffer[3] == L'\\' &&
                MountPoint.Buffer[5] == ':')
            {
                if (lpszVolumeName != NULL && cchBufferLength >= 1)
                {
                    lpszVolumeName[0] = UNICODE_NULL;
                }
            }
            /* Maybe it's a reparse point? */
            else
            {
                Ret = BasepGetVolumeNameFromReparsePoint(lpszMountPoint, lpszVolumeName, cchBufferLength, IsAMountPoint);
            }
        }
    }

    return Ret;
}

/**
 * @name GetVolumeNameForVolumeMountPointW
 * @implemented
 *
 * Return an unique volume name for a drive root or mount point.
 *
 * @param VolumeMountPoint
 *        Pointer to string that contains either root drive name or
 *        mount point name.
 * @param VolumeName
 *        Pointer to buffer that is filled with resulting unique
 *        volume name on success.
 * @param VolumeNameLength
 *        Size of VolumeName buffer in TCHARs.
 *
 * @return
 *     TRUE when the function succeeds and the VolumeName buffer is filled,
 *     FALSE otherwise.
 */
BOOL
WINAPI
GetVolumeNameForVolumeMountPointW(IN LPCWSTR VolumeMountPoint,
                                  OUT LPWSTR VolumeName,
                                  IN DWORD VolumeNameLength)
{
    BOOL Ret;

    /* Just query our internal function */
    Ret = BasepGetVolumeNameForVolumeMountPoint(VolumeMountPoint, VolumeName,
                                                VolumeNameLength, NULL);
    if (!Ret && VolumeName != NULL && VolumeNameLength >= 1)
    {
        VolumeName[0] = UNICODE_NULL;
    }

    return Ret;
}

static
BOOL
BasepIsDriveRootPath(IN LPCWSTR lpszPath)
{
    return lpszPath[0] != UNICODE_NULL && lpszPath[1] == L':' &&
           lpszPath[2] == L'\\' && lpszPath[3] == UNICODE_NULL;
}

static
BOOL
BasepMountMgrControl(IN DWORD IoControlCode,
                     IN PVOID InputBuffer,
                     IN DWORD InputLength,
                     OUT PVOID OutputBuffer,
                     IN DWORD OutputLength)
{
    BOOL Ret;
    DWORD BytesReturned;
    HANDLE MountMgrHandle;

    MountMgrHandle = CreateFileW(MOUNTMGR_DOS_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                 INVALID_HANDLE_VALUE);
    if (MountMgrHandle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    Ret = DeviceIoControl(MountMgrHandle, IoControlCode, InputBuffer, InputLength,
                          OutputBuffer, OutputLength, &BytesReturned, NULL);
    CloseHandle(MountMgrHandle);
    return Ret;
}

static
BOOL
BasepNotifyVolumeMountPoint(IN DWORD IoControlCode,
                            IN LPCWSTR lpszVolumeMountPoint,
                            IN PCUNICODE_STRING TargetVolumeName)
{
    BOOL Ret;
    ULONG Size;
    UNICODE_STRING SourceName;
    PMOUNTMGR_VOLUME_MOUNT_POINT MountPoint;

    if (!RtlDosPathNameToNtPathName_U(lpszVolumeMountPoint, &SourceName, NULL, NULL))
    {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }

    Size = sizeof(MOUNTMGR_VOLUME_MOUNT_POINT) + SourceName.Length + TargetVolumeName->Length;
    MountPoint = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (MountPoint == NULL)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, SourceName.Buffer);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    MountPoint->SourceVolumeNameOffset = sizeof(MOUNTMGR_VOLUME_MOUNT_POINT);
    MountPoint->SourceVolumeNameLength = SourceName.Length;
    MountPoint->TargetVolumeNameOffset = MountPoint->SourceVolumeNameOffset + SourceName.Length;
    MountPoint->TargetVolumeNameLength = TargetVolumeName->Length;
    RtlCopyMemory((PUCHAR)MountPoint + MountPoint->SourceVolumeNameOffset,
                  SourceName.Buffer, SourceName.Length);
    RtlCopyMemory((PUCHAR)MountPoint + MountPoint->TargetVolumeNameOffset,
                  TargetVolumeName->Buffer, TargetVolumeName->Length);

    Ret = BasepMountMgrControl(IoControlCode, MountPoint, Size, NULL, 0);

    RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoint);
    RtlFreeHeap(RtlGetProcessHeap(), 0, SourceName.Buffer);
    return Ret;
}

static
BOOL
BasepDeleteMountPointReparse(IN LPCWSTR lpszVolumeMountPoint)
{
    BOOL Ret;
    HANDLE Handle;
    DWORD BytesReturned;
    REPARSE_DATA_BUFFER ReparseBuffer;

    Handle = CreateFileW(lpszVolumeMountPoint, GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                         FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    RtlZeroMemory(&ReparseBuffer, FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer));
    ReparseBuffer.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    Ret = DeviceIoControl(Handle, FSCTL_DELETE_REPARSE_POINT, &ReparseBuffer,
                          FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer), NULL, 0, &BytesReturned, NULL);
    CloseHandle(Handle);
    return Ret;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetVolumeMountPointW(IN LPCWSTR lpszVolumeMountPoint,
                     IN LPCWSTR lpszVolumeName)
{
    BOOL Ret;
    ULONG Size;
    HANDLE Handle;
    DWORD BytesReturned;
    UNICODE_STRING VolumeName;
    UNICODE_STRING TargetName;
    WCHAR TargetBuffer[MAX_PATH];
    PREPARSE_DATA_BUFFER ReparseBuffer;
    PMOUNTMGR_CREATE_POINT_INPUT CreatePoint;
    WCHAR SymbolicLink[] = L"\\DosDevices\\?:";

    if (lpszVolumeMountPoint == NULL || lpszVolumeName == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    RtlInitUnicodeString(&VolumeName, lpszVolumeName);
    if (!MOUNTMGR_IS_DOS_VOLUME_NAME_WB(&VolumeName))
    {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }

    RtlCopyMemory(TargetBuffer, VolumeName.Buffer, VolumeName.Length);
    TargetBuffer[1] = L'?';
    TargetName.Buffer = TargetBuffer;
    TargetName.Length = VolumeName.Length - sizeof(WCHAR);
    TargetName.MaximumLength = TargetName.Length;

    if (BasepIsDriveRootPath(lpszVolumeMountPoint))
    {
        SymbolicLink[12] = towupper(lpszVolumeMountPoint[0]);

        Size = sizeof(MOUNTMGR_CREATE_POINT_INPUT) + sizeof(SymbolicLink) - sizeof(UNICODE_NULL) + TargetName.Length;
        CreatePoint = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Size);
        if (CreatePoint == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        CreatePoint->SymbolicLinkNameOffset = sizeof(MOUNTMGR_CREATE_POINT_INPUT);
        CreatePoint->SymbolicLinkNameLength = sizeof(SymbolicLink) - sizeof(UNICODE_NULL);
        CreatePoint->DeviceNameOffset = CreatePoint->SymbolicLinkNameOffset + CreatePoint->SymbolicLinkNameLength;
        CreatePoint->DeviceNameLength = TargetName.Length;
        RtlCopyMemory((PUCHAR)CreatePoint + CreatePoint->SymbolicLinkNameOffset,
                      SymbolicLink, CreatePoint->SymbolicLinkNameLength);
        RtlCopyMemory((PUCHAR)CreatePoint + CreatePoint->DeviceNameOffset,
                      TargetName.Buffer, TargetName.Length);

        Ret = BasepMountMgrControl(IOCTL_MOUNTMGR_CREATE_POINT, CreatePoint, Size, NULL, 0);
        RtlFreeHeap(RtlGetProcessHeap(), 0, CreatePoint);
        return Ret;
    }

    Size = FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer) +
           VolumeName.Length + 2 * sizeof(UNICODE_NULL);
    ReparseBuffer = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Size);
    if (ReparseBuffer == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }

    ReparseBuffer->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    ReparseBuffer->ReparseDataLength = (USHORT)(Size - FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer));
    ReparseBuffer->MountPointReparseBuffer.SubstituteNameOffset = 0;
    ReparseBuffer->MountPointReparseBuffer.SubstituteNameLength = VolumeName.Length;
    ReparseBuffer->MountPointReparseBuffer.PrintNameOffset = VolumeName.Length + sizeof(UNICODE_NULL);
    ReparseBuffer->MountPointReparseBuffer.PrintNameLength = 0;
    RtlCopyMemory(ReparseBuffer->MountPointReparseBuffer.PathBuffer, VolumeName.Buffer, VolumeName.Length);
    ReparseBuffer->MountPointReparseBuffer.PathBuffer[1] = L'?';

    Handle = CreateFileW(lpszVolumeMountPoint, GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                         FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, ReparseBuffer);
        return FALSE;
    }

    Ret = DeviceIoControl(Handle, FSCTL_SET_REPARSE_POINT, ReparseBuffer, Size,
                          NULL, 0, &BytesReturned, NULL);
    CloseHandle(Handle);
    RtlFreeHeap(RtlGetProcessHeap(), 0, ReparseBuffer);
    if (!Ret)
    {
        return FALSE;
    }

    if (!BasepNotifyVolumeMountPoint(IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_CREATED,
                                     lpszVolumeMountPoint, &TargetName))
    {
        DWORD Error = GetLastError();

        BasepDeleteMountPointReparse(lpszVolumeMountPoint);
        SetLastError(Error);
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
SetVolumeMountPointA(IN LPCSTR lpszVolumeMountPoint,
                     IN LPCSTR lpszVolumeName)
{
    BOOL Ret;
    UNICODE_STRING VolumeMountPointU;
    UNICODE_STRING VolumeNameU;

    if (!Basep8BitStringToDynamicUnicodeString(&VolumeMountPointU, lpszVolumeMountPoint))
    {
        return FALSE;
    }

    if (!Basep8BitStringToDynamicUnicodeString(&VolumeNameU, lpszVolumeName))
    {
        RtlFreeUnicodeString(&VolumeMountPointU);
        return FALSE;
    }

    Ret = SetVolumeMountPointW(VolumeMountPointU.Buffer, VolumeNameU.Buffer);

    RtlFreeUnicodeString(&VolumeNameU);
    RtlFreeUnicodeString(&VolumeMountPointU);
    return Ret;
}

/*
 * @implemented
 */
BOOL
WINAPI
DeleteVolumeMountPointA(IN LPCSTR lpszVolumeMountPoint)
{
    PUNICODE_STRING VolumeMountPointU;

    VolumeMountPointU = Basep8BitStringToStaticUnicodeString(lpszVolumeMountPoint);
    if (VolumeMountPointU == NULL)
    {
        return FALSE;
    }

    return DeleteVolumeMountPointW(VolumeMountPointU->Buffer);
}

/*
 * @implemented
 */
BOOL
WINAPI
DeleteVolumeMountPointW(IN LPCWSTR lpszVolumeMountPoint)
{
    BOOL Ret;
    ULONG Size;
    UNICODE_STRING TargetName;
    WCHAR TargetBuffer[MAX_PATH];
    PMOUNTMGR_MOUNT_POINT MountPoint;
    PMOUNTMGR_MOUNT_POINTS MountPoints;
    WCHAR SymbolicLink[] = L"\\DosDevices\\?:";

    if (lpszVolumeMountPoint == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (BasepIsDriveRootPath(lpszVolumeMountPoint))
    {
        SymbolicLink[12] = towupper(lpszVolumeMountPoint[0]);

        Size = sizeof(MOUNTMGR_MOUNT_POINT) + sizeof(SymbolicLink) - sizeof(UNICODE_NULL);
        MountPoint = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Size);
        if (MountPoint == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        MountPoints = RtlAllocateHeap(RtlGetProcessHeap(), 0, 4096);
        if (MountPoints == NULL)
        {
            RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoint);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        MountPoint->SymbolicLinkNameOffset = sizeof(MOUNTMGR_MOUNT_POINT);
        MountPoint->SymbolicLinkNameLength = sizeof(SymbolicLink) - sizeof(UNICODE_NULL);
        RtlCopyMemory((PUCHAR)MountPoint + MountPoint->SymbolicLinkNameOffset,
                      SymbolicLink, MountPoint->SymbolicLinkNameLength);

        Ret = BasepMountMgrControl(IOCTL_MOUNTMGR_DELETE_POINTS, MountPoint, Size, MountPoints, 4096);
        if (Ret && MountPoints->NumberOfMountPoints == 0)
        {
            SetLastError(ERROR_FILE_NOT_FOUND);
            Ret = FALSE;
        }

        RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoints);
        RtlFreeHeap(RtlGetProcessHeap(), 0, MountPoint);
        return Ret;
    }

    if (!BasepGetVolumeNameFromReparsePoint(lpszVolumeMountPoint, TargetBuffer,
                                            _countof(TargetBuffer), NULL))
    {
        if (GetLastError() == ERROR_SUCCESS)
            SetLastError(ERROR_NOT_A_REPARSE_POINT);
        return FALSE;
    }

    if (!BasepDeleteMountPointReparse(lpszVolumeMountPoint))
    {
        return FALSE;
    }

    TargetBuffer[1] = L'?';
    RtlInitUnicodeString(&TargetName, TargetBuffer);
    if (TargetName.Length > sizeof(WCHAR) && TargetName.Buffer[TargetName.Length / sizeof(WCHAR) - 1] == L'\\')
    {
        TargetName.Length -= sizeof(WCHAR);
    }

    BasepNotifyVolumeMountPoint(IOCTL_MOUNTMGR_VOLUME_MOUNT_POINT_DELETED,
                                lpszVolumeMountPoint, &TargetName);
    return TRUE;
}

/*
 * @unimplemented
 */
HANDLE
WINAPI
FindFirstVolumeMountPointW(IN LPCWSTR lpszRootPathName,
                           IN LPWSTR lpszVolumeMountPoint,
                           IN DWORD cchBufferLength)
{
    STUB;
    return 0;
}

/*
 * @unimplemented
 */
HANDLE
WINAPI
FindFirstVolumeMountPointA(IN LPCSTR lpszRootPathName,
                           IN LPSTR lpszVolumeMountPoint,
                           IN DWORD cchBufferLength)
{
    STUB;
    return 0;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
FindNextVolumeMountPointA(IN HANDLE hFindVolumeMountPoint,
                          IN LPSTR lpszVolumeMountPoint,
                          DWORD cchBufferLength)
{
    STUB;
    return 0;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
FindNextVolumeMountPointW(IN HANDLE hFindVolumeMountPoint,
                          IN LPWSTR lpszVolumeMountPoint,
                          DWORD cchBufferLength)
{
    STUB;
    return 0;
}

/*
 * @unimplemented
 */
BOOL
WINAPI
FindVolumeMountPointClose(IN HANDLE hFindVolumeMountPoint)
{
    STUB;
    return 0;
}

/* EOF */
