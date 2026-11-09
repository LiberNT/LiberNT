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

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * A name of the shape "C:" in a DOS devices directory is a drive letter, and
 * the index it stands for is kept one higher so that zero means "not a drive".
 */
static
ULONG
ObpDriveIndexFromName(
    _In_ PUNICODE_STRING Name)
{
    WCHAR Letter;

    if (Name->Length != 2 * sizeof(WCHAR)) return 0;
    if (Name->Buffer[1] != L':') return 0;

    Letter = RtlUpcaseUnicodeChar(Name->Buffer[0]);
    if (Letter < L'A' || Letter > L'Z') return 0;

    return (ULONG)(Letter - L'A') + 1;
}

/* Reads back what kind of medium a drive letter's target turned out to be. */
static
UCHAR
ObpDriveTypeOfDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    switch (DeviceObject->DeviceType)
    {
        case FILE_DEVICE_CD_ROM:
        case FILE_DEVICE_CD_ROM_FILE_SYSTEM:
            return DOSDEVICE_DRIVE_CDROM;

        case FILE_DEVICE_NETWORK:
        case FILE_DEVICE_NETWORK_FILE_SYSTEM:
            return DOSDEVICE_DRIVE_REMOTE;

        case FILE_DEVICE_VIRTUAL_DISK:
            return DOSDEVICE_DRIVE_RAMDISK;

        case FILE_DEVICE_DISK:
        case FILE_DEVICE_DISK_FILE_SYSTEM:
        case FILE_DEVICE_FILE_SYSTEM:
            if (DeviceObject->Characteristics & FILE_REMOVABLE_MEDIA)
                return DOSDEVICE_DRIVE_REMOVABLE;

            return DOSDEVICE_DRIVE_FIXED;

        default:
            return DOSDEVICE_DRIVE_UNKNOWN;
    }
}

/*
 * Works out what a drive letter points at. Until the target resolves, the
 * letter is published as still to be worked out rather than as nothing.
 */
static
UCHAR
ObpResolveDriveType(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink)
{
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;
    UCHAR DriveType;
    PVOID Object;

    UNICODE_STRING Parent = SymbolicLink->LinkTarget;
    UNICODE_STRING Leaf;
    POBJECT_DIRECTORY Directory;
    OBP_LOOKUP_CONTEXT Context;
    USHORT Index;

    if (Parent.Length <= sizeof(WCHAR)) return DOSDEVICE_DRIVE_CALCULATE;

    /* Split the target into the directory that holds it and its own name */
    for (Index = Parent.Length / sizeof(WCHAR); Index > 0; Index--)
    {
        if (Parent.Buffer[Index - 1] == OBJ_NAME_PATH_SEPARATOR) break;
    }

    if (Index <= 1) return DOSDEVICE_DRIVE_CALCULATE;

    Leaf.Buffer = &Parent.Buffer[Index];
    Leaf.Length = Parent.Length - (Index * sizeof(WCHAR));
    Leaf.MaximumLength = Leaf.Length;
    if (Leaf.Length == 0) return DOSDEVICE_DRIVE_CALCULATE;

    Parent.Length = (Index - 1) * sizeof(WCHAR);
    Parent.MaximumLength = Parent.Length;

    Status = ObReferenceObjectByName(&Parent,
                                     OBJ_CASE_INSENSITIVE,
                                     NULL,
                                     0,
                                     ObpDirectoryObjectType,
                                     KernelMode,
                                     NULL,
                                     (PVOID *)&Directory);
    if (!NT_SUCCESS(Status)) return DOSDEVICE_DRIVE_CALCULATE;

    DriveType = DOSDEVICE_DRIVE_CALCULATE;

    ObpInitializeLookupContext(&Context);
    Object = ObpLookupEntryDirectory(Directory, &Leaf, OBJ_CASE_INSENSITIVE,
                                     FALSE, &Context);
    if (Object != NULL &&
        ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(Object)) ==
        IoDeviceObjectType)
    {
        DeviceObject = Object;
        DriveType = ObpDriveTypeOfDevice(DeviceObject);
    }

    ObpReleaseLookupContext(&Context);
    ObDereferenceObject(Directory);

    return DriveType;
}

/* Publishes a drive letter in the device map of the directory holding it. */
VOID
NTAPI
ObpCreateSymbolicLinkName(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(SymbolicLink);
    POBJECT_HEADER_NAME_INFO NameInfo;
    PDEVICE_MAP DeviceMap;
    ULONG Index;

    SymbolicLink->DosDeviceDriveIndex = 0;

    NameInfo = ObpReferenceNameInfo(Header);
    if (NameInfo == NULL) return;

    if (NameInfo->Directory == NULL)
    {
        ObpDereferenceNameInfo(NameInfo);
        return;
    }

    Index = ObpDriveIndexFromName(&NameInfo->Name);
    DeviceMap = NameInfo->Directory->DeviceMap;

    if (Index != 0 && DeviceMap != NULL)
    {
        SymbolicLink->DosDeviceDriveIndex = Index;

        KeAcquireGuardedMutex(&ObpDeviceMapLock);
        DeviceMap->DriveType[Index - 1] = ObpResolveDriveType(SymbolicLink);
        DeviceMap->DriveMap |= 1u << (Index - 1);
        KeReleaseGuardedMutex(&ObpDeviceMapLock);
    }

    ObpDereferenceNameInfo(NameInfo);
}

VOID
NTAPI
ObpDeleteSymbolicLinkName(
    _In_ POBJECT_SYMBOLIC_LINK SymbolicLink)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(SymbolicLink);
    POBJECT_HEADER_NAME_INFO NameInfo;
    PDEVICE_MAP DeviceMap;
    ULONG Index = SymbolicLink->DosDeviceDriveIndex;

    if (Index == 0) return;

    NameInfo = ObpReferenceNameInfo(Header);
    if (NameInfo == NULL) return;

    DeviceMap = (NameInfo->Directory != NULL) ?
                NameInfo->Directory->DeviceMap : NULL;

    if (DeviceMap != NULL)
    {
        KeAcquireGuardedMutex(&ObpDeviceMapLock);
        DeviceMap->DriveType[Index - 1] = DOSDEVICE_DRIVE_UNKNOWN;
        DeviceMap->DriveMap &= ~(1u << (Index - 1));
        KeReleaseGuardedMutex(&ObpDeviceMapLock);
    }

    SymbolicLink->DosDeviceDriveIndex = 0;
    ObpDereferenceNameInfo(NameInfo);
}

/* OBJECT TYPE METHODS ********************************************************/

VOID
NTAPI
ObpDeleteSymbolicLink(
    _In_ PVOID ObjectBody)
{
    POBJECT_SYMBOLIC_LINK SymbolicLink = ObjectBody;
    if (SymbolicLink->LinkTargetObject != NULL)
    {
        ObDereferenceObject(SymbolicLink->LinkTargetObject);
        SymbolicLink->LinkTargetObject = NULL;
    }

    if (SymbolicLink->LinkTarget.Buffer != NULL)
    {
        ExFreePoolWithTag(SymbolicLink->LinkTarget.Buffer, TAG_SYMLINK_TARGET);
        RtlInitEmptyUnicodeString(&SymbolicLink->LinkTarget, NULL, 0);
    }
}

/*
 * Resolving a link means handing the lookup a whole new path: the target with
 * whatever of the original path was left over appended to it.
 */
static
NTSTATUS
ObpBuildReparsePath(
    _In_ PUNICODE_STRING Target,
    _In_ PUNICODE_STRING Remaining,
    _Inout_ PUNICODE_STRING FullPath)
{
    ULONG Length, Maximum;
    PWCHAR Buffer;

    /* What is left of the path still carries its own separator */
    Length = Target->Length + Remaining->Length;

    if (Length > MAXUSHORT - sizeof(UNICODE_NULL))
        return STATUS_NAME_TOO_LONG;

    /* The name buffer is freed by its length, so a replacement must never come
     * out exactly the size the lookaside hands out */
    Maximum = Length + sizeof(UNICODE_NULL);
    if (Maximum == OBP_NAME_LOOKASIDE_MAX_SIZE)
        Maximum += sizeof(UNICODE_NULL);

    Buffer = ExAllocatePoolWithTag(PagedPool, Maximum, OB_NAME_TAG);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, Target->Buffer, Target->Length);
    if (Remaining->Length != 0)
    {
        RtlCopyMemory(&Buffer[Target->Length / sizeof(WCHAR)],
                      Remaining->Buffer,
                      Remaining->Length);
    }

    Buffer[Length / sizeof(WCHAR)] = UNICODE_NULL;

    ObpFreeObjectNameBuffer(FullPath);

    FullPath->Buffer = Buffer;
    FullPath->Length = (USHORT)Length;
    FullPath->MaximumLength = (USHORT)Maximum;

    Remaining->Buffer = NULL;
    Remaining->Length = 0;
    Remaining->MaximumLength = 0;

    return STATUS_SUCCESS;
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
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(AccessState);
    UNREFERENCED_PARAMETER(AccessMode);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(SecurityQos);

    PAGED_CODE();

    *NextObject = NULL;

    /* A caller after the link itself, rather than after what it points at,
     * is at the end of the path and says so by type or by attribute */
    if (RemainingName->Length == 0 &&
        (ObjectType == ObpSymbolicLinkObjectType ||
         (Attributes & OBJ_OPENLINK)))
    {
        *NextObject = ParsedObject;
        return STATUS_SUCCESS;
    }

    /* A link that already knows its target object short-circuits the walk */
    if (SymbolicLink->LinkTargetObject != NULL &&
        SymbolicLink->LinkTargetRemaining.Length == 0 &&
        RemainingName->Length == 0)
    {
        ObReferenceObject(SymbolicLink->LinkTargetObject);
        *NextObject = SymbolicLink->LinkTargetObject;
        return STATUS_REPARSE_OBJECT;
    }

    Status = ObpBuildReparsePath(&SymbolicLink->LinkTarget,
                                 RemainingName,
                                 FullPath);
    if (!NT_SUCCESS(Status)) return Status;

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
    return ObpParseSymbolicLink(ParsedObject, ObjectType, AccessState,
                                AccessMode, Attributes, FullPath, RemainingName,
                                Context, SecurityQos, NextObject);
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
    UNICODE_STRING Captured;
    NTSTATUS Status;
    HANDLE Handle;
    PWCHAR Buffer;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(LinkHandle);
            Captured = ProbeForReadUnicodeString(LinkTarget);
            if (Captured.Length != 0)
                ProbeForRead(Captured.Buffer, Captured.Length, sizeof(WCHAR));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Captured = *LinkTarget;
    }

    /* An empty target names the namespace root, which is how GLOBALROOT works */
    if (Captured.Length > MAXUSHORT - sizeof(UNICODE_NULL))
        return STATUS_NAME_TOO_LONG;

    /* The link keeps a copy of its target, terminated only when the caller's
     * own buffer had the room for a terminator */
    Buffer = ExAllocatePoolWithTag(PagedPool,
                                   Captured.Length + sizeof(UNICODE_NULL),
                                   TAG_SYMLINK_TARGET);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    _SEH2_TRY
    {
        RtlCopyMemory(Buffer, Captured.Buffer, Captured.Length);
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_SYMLINK_TARGET);
        return Status;
    }

    Buffer[Captured.Length / sizeof(WCHAR)] = UNICODE_NULL;

    Status = ObCreateObject(PreviousMode,
                            ObpSymbolicLinkObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(OBJECT_SYMBOLIC_LINK),
                            0,
                            0,
                            (PVOID *)&SymbolicLink);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_SYMLINK_TARGET);
        return Status;
    }

    KeQuerySystemTime(&SymbolicLink->CreationTime);
    SymbolicLink->LinkTarget.Buffer = Buffer;
    SymbolicLink->LinkTarget.Length = Captured.Length;
    SymbolicLink->LinkTarget.MaximumLength =
        (Captured.MaximumLength >= Captured.Length + sizeof(UNICODE_NULL)) ?
        Captured.Length + sizeof(UNICODE_NULL) : Captured.Length;
    RtlInitEmptyUnicodeString(&SymbolicLink->LinkTargetRemaining, NULL, 0);
    SymbolicLink->LinkTargetObject = NULL;
    SymbolicLink->DosDeviceDriveIndex = 0;

    Status = ObInsertObject(SymbolicLink,
                            NULL,
                            DesiredAccess,
                            0,
                            NULL,
                            &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        *LinkHandle = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
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
    NTSTATUS Status;
    HANDLE Handle;

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
                                &Handle);

    /* The handle is always reported, so a caller that primed its variable
     * sees it cleared rather than left behind */
    _SEH2_TRY
    {
        *LinkHandle = NT_SUCCESS(Status) ? Handle : NULL;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
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
    POBJECT_SYMBOLIC_LINK SymbolicLink;
    UNICODE_STRING Captured;
    NTSTATUS Status;
    ULONG Needed, Wanted;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteUnicodeString(LinkTarget);
            Captured = *LinkTarget;
            if (Captured.MaximumLength != 0)
            {
                ProbeForWrite(Captured.Buffer,
                              Captured.MaximumLength,
                              sizeof(WCHAR));
            }

            if (ResultLength != NULL) ProbeForWriteUlong(ResultLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Captured = *LinkTarget;
    }

    Status = ObReferenceObjectByHandle(LinkHandle,
                                       SYMBOLIC_LINK_QUERY,
                                       ObpSymbolicLinkObjectType,
                                       PreviousMode,
                                       (PVOID *)&SymbolicLink,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /* What the link reports it needs is the copy it keeps, terminator and all */
    Needed = SymbolicLink->LinkTarget.MaximumLength;

    /* Asking for the length as well asks for the whole stored copy, terminator
     * and all; asking only for the string takes the characters on their own */
    Wanted = (ResultLength != NULL) ? Needed : SymbolicLink->LinkTarget.Length;

    _SEH2_TRY
    {
        if (Captured.MaximumLength < Wanted)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            RtlCopyMemory(Captured.Buffer,
                          SymbolicLink->LinkTarget.Buffer,
                          Wanted);

            LinkTarget->Length = SymbolicLink->LinkTarget.Length;
            Status = STATUS_SUCCESS;
        }

        if (ResultLength != NULL) *ResultLength = Needed;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(SymbolicLink);
    return Status;
}

/* EOF */
