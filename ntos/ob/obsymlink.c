/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Symbolic link objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE ObpSymbolicLinkObjectType = NULL;

#define OBP_MAX_TARGET_REPARSE 32

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Splits the leading component off a target path. Returns FALSE when nothing
 * is left to take, which ends the walk.
 */
static
BOOLEAN
ObpTakePathComponent(
    _Inout_ PUNICODE_STRING Path,
    _Out_ PUNICODE_STRING Component)
{
    *Component = *Path;

    if (Path->Length != 0 && Path->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        Path->Buffer++;
        Path->Length -= sizeof(WCHAR);
        Component->Buffer = Path->Buffer;
        Component->Length = Path->Length;
        Component->MaximumLength = Path->MaximumLength;
    }

    while (Path->Length != 0 && Path->Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
    {
        Path->Buffer++;
        Path->Length -= sizeof(WCHAR);
    }

    Component->Length -= Path->Length;
    return (Component->Length != 0);
}

/*
 * Follows the link's target through the namespace to whatever device object it
 * ends at, so that the drive type can be taken from it. Returns NULL when the
 * target does not resolve, and sets Abandoned when the path is malformed.
 */
static
PVOID
ObpResolveLinkTargetDevice(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink,
    _In_opt_ POBJECT_DIRECTORY NameDirectory,
    _Inout_ POBP_LOOKUP_CONTEXT LookupContext,
    _Out_ PBOOLEAN Abandoned)
{
    UNICODE_STRING DECLSPEC_ALIGN(8) LocalTarget = SymbolicLink->LinkTarget;
    UNICODE_STRING DECLSPEC_ALIGN(8) Component;
    POBJECT_DIRECTORY DirectoryObject = ObpRootDirectoryObject;
    ULONG MaxReparse = OBP_MAX_TARGET_REPARSE;
    BOOLEAN DirectoryLocked;
    BOOLEAN Restart = TRUE;
    POBJECT_TYPE ObjectType;
    PDEVICE_MAP DeviceMap;
    PVOID Object = NULL;

    *Abandoned = FALSE;

    if (ObpLUIDDeviceMapsEnabled != 0)
        DeviceMap = ObSystemDeviceMap;
    else
        DeviceMap = ExGetObjectFastReference(PsGetCurrentProcess()->DeviceMap);

    while (Restart)
    {
        Restart = FALSE;

        /* A target under \?? is resolved inside the caller's own DOS devices */
        if (DeviceMap != NULL &&
            !((ULONG_PTR)LocalTarget.Buffer & 7) &&
            DeviceMap->DosDevicesDirectory != NULL &&
            LocalTarget.Length >= ObpDosDevicesShortName.Length &&
            *(PULONGLONG)LocalTarget.Buffer == ObpDosDevicesShortNamePrefix.Alignment.QuadPart)
        {
            DirectoryObject = DeviceMap->DosDevicesDirectory;
            LocalTarget.Length -= ObpDosDevicesShortName.Length;
            LocalTarget.Buffer += (ObpDosDevicesShortName.Length / sizeof(WCHAR));
        }

        while (!Restart)
        {
            if (!ObpTakePathComponent(&LocalTarget, &Component))
            {
                *Abandoned = TRUE;
                return NULL;
            }

            /* The link's own directory is already held by the caller */
            DirectoryLocked = LookupContext->DirectoryLocked;
            if (DirectoryObject == NameDirectory) LookupContext->DirectoryLocked = TRUE;

            Object = ObpLookupEntryDirectory(DirectoryObject, &Component, 0, FALSE,
                                             LookupContext);

            if (DirectoryObject == NameDirectory)
                LookupContext->DirectoryLocked = DirectoryLocked;

            if (Object == NULL) return NULL;

            ObjectType = ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(Object));
            if (ObjectType == ObpDirectoryObjectType)
            {
                DirectoryObject = Object;
                continue;
            }

            /* Anything that is not another drive letter ends the walk */
            if (ObjectType != ObpSymbolicLinkObjectType ||
                ((POBJECT_SYMBOLIC_LINK)Object)->DosDeviceDriveIndex != 0)
            {
                return Object;
            }

            if (MaxReparse == 0) return NULL;
            MaxReparse--;

            DirectoryObject = ObpRootDirectoryObject;
            LocalTarget = ((POBJECT_SYMBOLIC_LINK)Object)->LinkTarget;
            Restart = TRUE;
        }
    }

    return Object;
}

static
PDEVICE_MAP
ObpDeviceMapForLink(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink)
{
    POBJECT_HEADER_NAME_INFO ObjectNameInfo;
    PDEVICE_MAP DeviceMap = NULL;

    if (SymbolicLink->DosDeviceDriveIndex == 0) return NULL;

    ObjectNameInfo = ObpReferenceNameInfo(OBJECT_TO_OBJECT_HEADER(SymbolicLink));
    if (ObjectNameInfo == NULL) return NULL;

    if (ObjectNameInfo->Directory != NULL)
        DeviceMap = ObjectNameInfo->Directory->DeviceMap;

    ObpDereferenceNameInfo(ObjectNameInfo);
    return DeviceMap;
}

static
ULONG
ObpDriveTypeFromDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PCUNICODE_STRING LinkTarget)
{
    switch (DeviceObject->DeviceType)
    {
        case FILE_DEVICE_VIRTUAL_DISK:
            return DOSDEVICE_DRIVE_RAMDISK;

        case FILE_DEVICE_CD_ROM:
        case FILE_DEVICE_CD_ROM_FILE_SYSTEM:
            return DOSDEVICE_DRIVE_CDROM;

        case FILE_DEVICE_DISK:
        case FILE_DEVICE_DISK_FILE_SYSTEM:
        case FILE_DEVICE_FILE_SYSTEM:
            return (DeviceObject->Characteristics & FILE_REMOVABLE_MEDIA) ?
                   DOSDEVICE_DRIVE_REMOVABLE : DOSDEVICE_DRIVE_FIXED;

        case FILE_DEVICE_NETWORK:
        case FILE_DEVICE_NETWORK_FILE_SYSTEM:
            return DOSDEVICE_DRIVE_REMOTE;

        default:
            DPRINT1("Device Type %lu for %wZ is not known or unhandled\n",
                    DeviceObject->DeviceType, LinkTarget);
            return DOSDEVICE_DRIVE_UNKNOWN;
    }
}

VOID
ObpProcessDosDeviceSymbolicLink(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink,
    _In_ BOOLEAN DeleteLink)
{
    POBJECT_HEADER_NAME_INFO ObjectNameInfo;
    POBJECT_DIRECTORY NameDirectory = NULL;
    OBP_LOOKUP_CONTEXT LookupContext;
    BOOLEAN Abandoned = FALSE;
    PDEVICE_MAP DeviceMap;
    PVOID Object = NULL;
    ULONG DriveType;

    ObjectNameInfo = OBJECT_HEADER_TO_NAME_INFO(OBJECT_TO_OBJECT_HEADER(SymbolicLink));
    if (ObjectNameInfo != NULL) NameDirectory = ObjectNameInfo->Directory;

    ObpInitializeLookupContext(&LookupContext);

    /* Deleting a link that never resolved has nothing to look up */
    if (SymbolicLink->LinkTargetObject != NULL || !DeleteLink)
    {
        Object = ObpResolveLinkTargetDevice(SymbolicLink,
                                            NameDirectory,
                                            &LookupContext,
                                            &Abandoned);
        if (Abandoned)
        {
            ObpReleaseLookupContext(&LookupContext);
            return;
        }
    }

    DeviceMap = ObpDeviceMapForLink(SymbolicLink);

    if (DeleteLink)
    {
        RtlInitUnicodeString(&SymbolicLink->LinkTargetRemaining, NULL);

        if (SymbolicLink->LinkTargetObject != NULL)
        {
            ObDereferenceObject(SymbolicLink->LinkTargetObject);
            SymbolicLink->LinkTargetObject = NULL;
        }

        if (DeviceMap != NULL)
        {
            DeviceMap->DriveType[SymbolicLink->DosDeviceDriveIndex - 1] =
                DOSDEVICE_DRIVE_UNKNOWN;
            InterlockedAnd((PLONG)&DeviceMap->DriveMap,
                           ~(1 << (SymbolicLink->DosDeviceDriveIndex - 1)));
            SymbolicLink->DosDeviceDriveIndex = 0;
        }
    }
    else
    {
        /* Without a device at the end of the target the type stays deferred */
        DriveType = DOSDEVICE_DRIVE_CALCULATE;
        if (Object != NULL &&
            SymbolicLink->DosDeviceDriveIndex != 0 &&
            ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(Object)) == IoDeviceObjectType)
        {
            DriveType = ObpDriveTypeFromDevice((PDEVICE_OBJECT)Object,
                                               &SymbolicLink->LinkTarget);
        }

        if (DeviceMap != NULL)
        {
            DeviceMap->DriveType[SymbolicLink->DosDeviceDriveIndex - 1] = (UCHAR)DriveType;
            InterlockedOr((PLONG)&DeviceMap->DriveMap,
                          1 << (SymbolicLink->DosDeviceDriveIndex - 1));
        }
    }

    ObpReleaseLookupContext(&LookupContext);
}

VOID
NTAPI
ObpDeleteSymbolicLinkName(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink)
{
    ObpProcessDosDeviceSymbolicLink(SymbolicLink, TRUE);
}

VOID
NTAPI
ObpCreateSymbolicLinkName(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink)
{
    POBJECT_HEADER_NAME_INFO ObjectNameInfo;
    WCHAR UpperDrive;

    ObjectNameInfo = ObpReferenceNameInfo(OBJECT_TO_OBJECT_HEADER(SymbolicLink));
    if (ObjectNameInfo == NULL) return;

    if (ObjectNameInfo->Directory != NULL &&
        ObjectNameInfo->Directory->DeviceMap != NULL)
    {
        /* A two character name ending in a colon is a drive letter */
        if (ObjectNameInfo->Name.Length == 2 * sizeof(WCHAR) &&
            ObjectNameInfo->Name.Buffer[1] == L':')
        {
            UpperDrive = RtlUpcaseUnicodeChar(ObjectNameInfo->Name.Buffer[0]);
            if (UpperDrive >= L'A' && UpperDrive <= L'Z')
                SymbolicLink->DosDeviceDriveIndex = UpperDrive - (L'A' - 1);
        }

        ObpProcessDosDeviceSymbolicLink(SymbolicLink, FALSE);
    }

    ObpDereferenceNameInfo(ObjectNameInfo);
}

VOID
NTAPI
ObpDeleteSymbolicLink(
    _In_ PVOID ObjectBody)
{
    POBJECT_SYMBOLIC_LINK SymbolicLink = ObjectBody;

    if (SymbolicLink->LinkTarget.Buffer != NULL)
    {
        ExFreePool(SymbolicLink->LinkTarget.Buffer);
        SymbolicLink->LinkTarget.Buffer = NULL;
    }
}

/*
 * Rewrites the path being parsed so that it continues from the link's own
 * resolved target object, then hands the lookup back for another round.
 */
static
NTSTATUS
ObpReparseFromTargetObject(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink,
    _Inout_ PUNICODE_STRING FullPath,
    _Inout_ PUNICODE_STRING RemainingName)
{
    SIZE_T LengthUsed, MaximumLength, TempLength;
    SIZE_T Prefix;
    PWSTR NewTargetPath;

    if (SymbolicLink->LinkTargetRemaining.Length == 0) return STATUS_REPARSE_OBJECT;

    TempLength = SymbolicLink->LinkTargetRemaining.Length;
    if (RemainingName->Length != 0 &&
        SymbolicLink->LinkTargetRemaining.Buffer[TempLength / sizeof(WCHAR) - 1] ==
        OBJ_NAME_PATH_SEPARATOR &&
        RemainingName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        TempLength -= sizeof(OBJ_NAME_PATH_SEPARATOR);
    }

    Prefix = RemainingName->Buffer - FullPath->Buffer;
    LengthUsed = TempLength + RemainingName->Length + (sizeof(WCHAR) * Prefix);
    if (LengthUsed > 0xFFF0) return STATUS_NAME_TOO_LONG;

    /* The path still has room, so the insert happens where it stands */
    if (FullPath->MaximumLength > LengthUsed)
    {
        if (RemainingName->Length)
        {
            RtlMoveMemory((PVOID)((ULONG_PTR)RemainingName->Buffer + TempLength),
                          RemainingName->Buffer,
                          RemainingName->Length);
        }

        RtlCopyMemory(RemainingName->Buffer,
                      SymbolicLink->LinkTargetRemaining.Buffer,
                      TempLength);

        FullPath->Length += SymbolicLink->LinkTargetRemaining.Length;
        RemainingName->Length += SymbolicLink->LinkTargetRemaining.Length;
        RemainingName->MaximumLength += RemainingName->Length + sizeof(WCHAR);
        FullPath->Buffer[FullPath->Length / sizeof(WCHAR)] = UNICODE_NULL;
        return STATUS_REPARSE_OBJECT;
    }

    MaximumLength = LengthUsed + sizeof(WCHAR);
    NewTargetPath = ExAllocatePoolWithTag(NonPagedPool, MaximumLength, OB_NAME_TAG);
    if (NewTargetPath == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(NewTargetPath, FullPath->Buffer, sizeof(WCHAR) * Prefix);

    if (RemainingName->Length)
    {
        RtlCopyMemory((PVOID)((ULONG_PTR)&NewTargetPath[Prefix] + TempLength),
                      RemainingName->Buffer,
                      RemainingName->Length);
    }

    RtlCopyMemory(&NewTargetPath[Prefix],
                  SymbolicLink->LinkTargetRemaining.Buffer,
                  TempLength);

    ExFreePool(FullPath->Buffer);
    FullPath->Buffer = NewTargetPath;
    FullPath->MaximumLength = (USHORT)MaximumLength;
    FullPath->Length = (USHORT)LengthUsed;

    RemainingName->Length = (USHORT)(LengthUsed - sizeof(WCHAR) * Prefix);
    RemainingName->Buffer = &NewTargetPath[Prefix];
    RemainingName->MaximumLength = RemainingName->Length + sizeof(WCHAR);

    return STATUS_REPARSE_OBJECT;
}

NTSTATUS
NTAPI
ObpParseSymbolicLink(
    _In_ PVOID ParsedObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING FullPath,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Out_ PVOID *NextObject)
{
    POBJECT_SYMBOLIC_LINK SymbolicLink = ParsedObject;
    SIZE_T LengthUsed, MaximumLength, TempLength;
    PUNICODE_STRING TargetPath;
    PWSTR NewTargetPath;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(AccessState);
    UNREFERENCED_PARAMETER(Attributes);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(SecurityQos);

    PAGED_CODE();

    *NextObject = NULL;

    if (RemainingName->Length == 0)
    {
        /* The caller asked for the link itself, so hand it over as it is */
        if (ObjectType == ObpSymbolicLinkObjectType)
        {
            Status = ObReferenceObjectByPointer(ParsedObject, 0, ObjectType, AccessMode);
            if (NT_SUCCESS(Status)) *NextObject = ParsedObject;
            if (NT_SUCCESS(Status) || Status != STATUS_OBJECT_TYPE_MISMATCH) return Status;
        }
    }
    else if (RemainingName->Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
    {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }

    if (SymbolicLink->LinkTargetObject != NULL)
        return ObpReparseFromTargetObject(SymbolicLink, FullPath, RemainingName);

    TargetPath = &SymbolicLink->LinkTarget;
    TempLength = TargetPath->Length;

    /* Two separators would make an empty component out of the join */
    if (TempLength != 0 && RemainingName->Length != 0 &&
        TargetPath->Buffer[TempLength / sizeof(WCHAR) - 1] == OBJ_NAME_PATH_SEPARATOR &&
        RemainingName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        TempLength -= sizeof(OBJ_NAME_PATH_SEPARATOR);
    }

    LengthUsed = TempLength + RemainingName->Length;
    if (LengthUsed > 0xFFF0) return STATUS_NAME_TOO_LONG;

    if (FullPath->MaximumLength <= LengthUsed)
    {
        MaximumLength = LengthUsed + sizeof(WCHAR);
        NewTargetPath = ExAllocatePoolWithTag(NonPagedPool, MaximumLength, OB_NAME_TAG);
        if (NewTargetPath == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        MaximumLength = FullPath->MaximumLength;
        NewTargetPath = FullPath->Buffer;
    }

    if (RemainingName->Length)
    {
        RtlMoveMemory((PVOID)((ULONG_PTR)NewTargetPath + TempLength),
                      RemainingName->Buffer,
                      RemainingName->Length);
    }

    RtlCopyMemory(NewTargetPath, TargetPath->Buffer, TempLength);
    NewTargetPath[LengthUsed / sizeof(WCHAR)] = UNICODE_NULL;

    if (NewTargetPath != FullPath->Buffer) ExFreePool(FullPath->Buffer);

    FullPath->Length = (USHORT)LengthUsed;
    FullPath->MaximumLength = (USHORT)MaximumLength;
    FullPath->Buffer = NewTargetPath;

    return STATUS_REPARSE;
}

NTSTATUS
NTAPI
ObpParseSymbolicLinkEx(
    _In_ PVOID ParsedObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING FullPath,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _In_ POB_EXTENDED_PARSE_PARAMETERS ExtendedParameters,
    _Out_ PVOID *NextObject)
{
    UNREFERENCED_PARAMETER(ExtendedParameters);

    return ObpParseSymbolicLink(ParsedObject,
                                ObjectType,
                                AccessState,
                                AccessMode,
                                Attributes,
                                FullPath,
                                RemainingName,
                                Context,
                                SecurityQos,
                                NextObject);
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtCreateSymbolicLinkObject(
    _Out_ PHANDLE LinkHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ PUNICODE_STRING LinkTarget)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    POBJECT_SYMBOLIC_LINK SymbolicLink;
    UNICODE_STRING CapturedLinkTarget;
    NTSTATUS Status;
    HANDLE hLink;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            CapturedLinkTarget = ProbeForReadUnicodeString(LinkTarget);
            ProbeForRead(CapturedLinkTarget.Buffer,
                         CapturedLinkTarget.MaximumLength,
                         sizeof(WCHAR));
            ProbeForWriteHandle(LinkHandle);
            (VOID)ProbeForReadUlong(&ObjectAttributes->Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        CapturedLinkTarget = *LinkTarget;
    }

    if (CapturedLinkTarget.MaximumLength % sizeof(WCHAR))
    {
        CapturedLinkTarget.MaximumLength =
            (USHORT)ALIGN_DOWN(CapturedLinkTarget.MaximumLength, WCHAR);
    }

    if ((CapturedLinkTarget.Length % sizeof(WCHAR)) ||
        (CapturedLinkTarget.MaximumLength < CapturedLinkTarget.Length) ||
        (CapturedLinkTarget.MaximumLength == 0))
    {
        DbgPrint("OB: Invalid symbolic link target - %wZ\n", &CapturedLinkTarget);
        return STATUS_INVALID_PARAMETER;
    }

    Status = ObCreateObject(PreviousMode,
                            ObpSymbolicLinkObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(OBJECT_SYMBOLIC_LINK),
                            0,
                            0,
                            (PVOID *)&SymbolicLink);
    if (!NT_SUCCESS(Status)) return Status;

    KeQuerySystemTime(&SymbolicLink->CreationTime);

    SymbolicLink->LinkTarget.Length = CapturedLinkTarget.Length;
    SymbolicLink->LinkTarget.MaximumLength = CapturedLinkTarget.MaximumLength;
    SymbolicLink->LinkTarget.Buffer = ExAllocatePoolWithTag(PagedPool,
                                                            CapturedLinkTarget.MaximumLength,
                                                            TAG_SYMLINK_TARGET);
    if (SymbolicLink->LinkTarget.Buffer == NULL)
    {
        ObDereferenceObject(SymbolicLink);
        return STATUS_NO_MEMORY;
    }

    _SEH2_TRY
    {
        RtlCopyMemory(SymbolicLink->LinkTarget.Buffer,
                      CapturedLinkTarget.Buffer,
                      CapturedLinkTarget.MaximumLength);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ObDereferenceObject(SymbolicLink);
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    SymbolicLink->LinkTargetObject = NULL;
    SymbolicLink->DosDeviceDriveIndex = 0;
    RtlInitUnicodeString(&SymbolicLink->LinkTargetRemaining, NULL);

    Status = ObInsertObject(SymbolicLink, NULL, DesiredAccess, 0, NULL, &hLink);
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        *LinkHandle = hLink;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtOpenSymbolicLinkObject(
    _Out_ PHANDLE LinkHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE hLink = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(LinkHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                ObpSymbolicLinkObjectType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &hLink);

    /* The handle is written even on failure, so a caller sees it cleared */
    _SEH2_TRY
    {
        *LinkHandle = hLink;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtQuerySymbolicLinkObject(
    _In_ HANDLE LinkHandle,
    _Inout_ PUNICODE_STRING LinkTarget,
    _Out_opt_ PULONG ResultLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    UNICODE_STRING SafeLinkTarget = { 0, 0, NULL };
    POBJECT_SYMBOLIC_LINK SymbolicLink;
    POBJECT_HEADER ObjectHeader;
    NTSTATUS Status;
    ULONG LengthUsed;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUnicodeString(LinkTarget);
            SafeLinkTarget = *LinkTarget;
            ProbeForWrite(SafeLinkTarget.Buffer,
                          SafeLinkTarget.MaximumLength,
                          sizeof(WCHAR));
            if (ResultLength) ProbeForWriteUlong(ResultLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        SafeLinkTarget = *LinkTarget;
    }

    Status = ObReferenceObjectByHandle(LinkHandle,
                                       SYMBOLIC_LINK_QUERY,
                                       ObpSymbolicLinkObjectType,
                                       PreviousMode,
                                       (PVOID *)&SymbolicLink,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ObjectHeader = OBJECT_TO_OBJECT_HEADER(SymbolicLink);
    ObpAcquireObjectLock(ObjectHeader);

    /* A caller that wants the length back is also given the terminator */
    LengthUsed = ResultLength ? SymbolicLink->LinkTarget.MaximumLength :
                                SymbolicLink->LinkTarget.Length;

    _SEH2_TRY
    {
        if (LengthUsed <= SafeLinkTarget.MaximumLength)
        {
            RtlCopyMemory(SafeLinkTarget.Buffer,
                          SymbolicLink->LinkTarget.Buffer,
                          LengthUsed);
            LinkTarget->Length = SymbolicLink->LinkTarget.Length;
        }
        else
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }

        if (ResultLength) *ResultLength = SymbolicLink->LinkTarget.MaximumLength;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObpReleaseObjectLock(ObjectHeader);
    ObDereferenceObject(SymbolicLink);

    return Status;
}

/* EOF */
