/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The object namespace
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

ULONG ObpCaseInsensitive = TRUE;
POBJECT_DIRECTORY ObpRootDirectoryObject;
POBJECT_DIRECTORY ObpTypeDirectoryObject;

/* "\??" reached through the process device map, and its "\??\" prefix */
ALIGNEDNAME ObpDosDevicesShortNamePrefix = {{L'\\', L'?', L'?', L'\\'}};
ALIGNEDNAME ObpDosDevicesShortNameRoot = {{L'\\', L'?', L'?', L'\0'}};
UNICODE_STRING ObpDosDevicesShortName =
{
    sizeof(ObpDosDevicesShortNamePrefix),
    sizeof(ObpDosDevicesShortNamePrefix),
    (PWSTR)&ObpDosDevicesShortNamePrefix
};

WCHAR ObpUnsecureGlobalNamesBuffer[128] = {0};
ULONG ObpUnsecureGlobalNamesLength = sizeof(ObpUnsecureGlobalNamesBuffer);

/* The number of reparses one lookup may follow before it gives up. */
#define OBP_MAX_REPARSE 30

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Takes the leading component off a path. The separator that introduces it is
 * stepped over, and the one that ends it is left in place, because a parse
 * routine is handed a path that still begins with a separator.
 */
static
BOOLEAN
ObpTakeComponent(
    _Inout_ PUNICODE_STRING Remaining,
    _Out_ PUNICODE_STRING Component)
{
    USHORT Count, Index;

    if (Remaining->Length != 0 &&
        Remaining->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        Remaining->Buffer++;
        Remaining->Length -= sizeof(WCHAR);
        Remaining->MaximumLength = Remaining->Length;
    }

    if (Remaining->Length == 0) return FALSE;

    Count = Remaining->Length / sizeof(WCHAR);
    for (Index = 0; Index < Count; Index++)
    {
        if (Remaining->Buffer[Index] == OBJ_NAME_PATH_SEPARATOR) break;
    }

    Component->Buffer = Remaining->Buffer;
    Component->Length = Index * sizeof(WCHAR);
    Component->MaximumLength = Component->Length;

    Remaining->Buffer += Index;
    Remaining->Length -= Index * sizeof(WCHAR);
    Remaining->MaximumLength = Remaining->Length;

    return TRUE;
}

/*
 * "\??" and everything under it belong to the device map of whoever is asking,
 * so the path is cut back to the directory that map names.
 */
static
NTSTATUS
ObpRedirectDosDevices(
    _Inout_ PUNICODE_STRING Remaining,
    _Outptr_ POBJECT_DIRECTORY *Directory)
{
    PDEVICE_MAP DeviceMap;
    BOOLEAN Prefixed;

    if (Remaining->Length < ObpDosDevicesShortName.Length - sizeof(WCHAR))
        return STATUS_OBJECT_PATH_NOT_FOUND;

    /* The buffer need not be aligned for the quad-word compare the name is
     * laid out for, so the three or four characters are compared as bytes */
    if (Remaining->Length == ObpDosDevicesShortName.Length - sizeof(WCHAR))
    {
        if (!RtlEqualMemory(Remaining->Buffer,
                            ObpDosDevicesShortNameRoot.Name,
                            ObpDosDevicesShortName.Length - sizeof(WCHAR)))
        {
            return STATUS_OBJECT_PATH_NOT_FOUND;
        }

        Prefixed = FALSE;
    }
    else
    {
        if (!RtlEqualMemory(Remaining->Buffer,
                            ObpDosDevicesShortNamePrefix.Name,
                            ObpDosDevicesShortName.Length))
        {
            return STATUS_OBJECT_PATH_NOT_FOUND;
        }

        Prefixed = TRUE;
    }

    DeviceMap = ObpReferenceDeviceMap();
    if (DeviceMap == NULL || DeviceMap->DosDevicesDirectory == NULL)
    {
        if (DeviceMap != NULL) ObfDereferenceDeviceMap(DeviceMap);
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    /* Hand the directory over referenced, since the map goes back now */
    *Directory = DeviceMap->DosDevicesDirectory;
    ObReferenceObject(*Directory);
    ObfDereferenceDeviceMap(DeviceMap);

    if (Prefixed)
    {
        Remaining->Buffer += ObpDosDevicesShortName.Length / sizeof(WCHAR);
        Remaining->Length -= ObpDosDevicesShortName.Length;
    }
    else
    {
        Remaining->Buffer += Remaining->Length / sizeof(WCHAR);
        Remaining->Length = 0;
    }

    Remaining->MaximumLength = Remaining->Length;
    return STATUS_SUCCESS;
}

/* Keeps one reference of our own on a directory we are about to walk into. */
static
VOID
ObpHoldDirectory(
    _Inout_ POBJECT_DIRECTORY *Held,
    _In_opt_ POBJECT_DIRECTORY Directory)
{
    POBJECT_DIRECTORY Previous = *Held;

    if (Directory != NULL) ObReferenceObject(Directory);
    *Held = Directory;
    if (Previous != NULL) ObDereferenceObject(Previous);
}

static
NTSTATUS
ObpCallParseProcedure(
    _In_ POBJECT_TYPE ParseObjectType,
    _In_ PVOID ParseObject,
    _In_ POBJECT_TYPE ObjectType,
    _In_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING CompleteName,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID ParseContext,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Out_ PVOID *Object)
{
    OB_EXTENDED_PARSE_PARAMETERS ExtendedParameters;

    if (!ParseObjectType->TypeInfo.UseExtendedParameters)
    {
        return ParseObjectType->TypeInfo.ParseProcedure(ParseObject,
                                                        ObjectType,
                                                        AccessState,
                                                        AccessMode,
                                                        Attributes,
                                                        CompleteName,
                                                        RemainingName,
                                                        ParseContext,
                                                        SecurityQos,
                                                        Object);
    }

    RtlZeroMemory(&ExtendedParameters, sizeof(ExtendedParameters));
    ExtendedParameters.Length = sizeof(ExtendedParameters);
    ExtendedParameters.RestrictedAccessMask = MAXULONG;

    return ParseObjectType->TypeInfo.ParseProcedureEx(ParseObject,
                                                      ObjectType,
                                                      AccessState,
                                                      AccessMode,
                                                      Attributes,
                                                      CompleteName,
                                                      RemainingName,
                                                      ParseContext,
                                                      SecurityQos,
                                                      &ExtendedParameters,
                                                      Object);
}

/*
 * A name only lives as long as a handle or a permanent flag keeps it, so the
 * last handle to a temporary object takes it out of the namespace.
 */
VOID
NTAPI
ObpDeleteNameCheck(
    _In_ PVOID Object)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE Type = ObpGetObjectTypeFromHeader(Header);
    POBJECT_HEADER_NAME_INFO NameInfo;
    OBP_LOOKUP_CONTEXT Context;
    BOOLEAN Unnamed = FALSE;

    NameInfo = ObpReferenceNameInfo(Header);
    if (NameInfo == NULL) return;

    if (NameInfo->Directory == NULL ||
        Header->HandleCount != 0 ||
        (Header->Flags & OB_FLAG_PERMANENT))
    {
        ObpDereferenceNameInfo(NameInfo);
        return;
    }

    ObpInitializeLookupContext(&Context);
    ObpAcquireLookupContextLock(&Context, NameInfo->Directory);

    /* The entry may already be gone, and the counts may have changed while the
     * directory lock was being taken */
    if (ObpLookupEntryDirectory(NameInfo->Directory,
                                &NameInfo->Name,
                                0,
                                FALSE,
                                &Context) == Object)
    {
        ObpAcquireObjectLock(Header);

        if (Header->HandleCount == 0 && !(Header->Flags & OB_FLAG_PERMANENT))
        {
            ObpDeleteEntryDirectory(&Context);
            Unnamed = TRUE;
        }

        ObpReleaseObjectLock(Header);
    }

    ObpReleaseLookupContext(&Context);

    if (Unnamed && Type == ObpSymbolicLinkObjectType)
        ObpDeleteSymbolicLinkName(Object);

    ObpDereferenceNameInfo(NameInfo);

    /* The name held one reference of its own */
    if (Unnamed) ObDereferenceObject(Object);
}

/* A name the registry marks unsecure may be created in the global namespace
 * by anyone, which is why the list comes from the configuration. */
static
BOOLEAN
ObpIsUnsecureName(
    _In_ PUNICODE_STRING Name,
    _In_ BOOLEAN CaseInsensitive)
{
    UNICODE_STRING Unsecure;
    PWCHAR Cursor = ObpUnsecureGlobalNamesBuffer;
    PWCHAR End = ObpUnsecureGlobalNamesBuffer +
                 (ObpUnsecureGlobalNamesLength / sizeof(WCHAR));

    while (Cursor < End && *Cursor != UNICODE_NULL)
    {
        RtlInitUnicodeString(&Unsecure, Cursor);
        if (RtlPrefixUnicodeString(&Unsecure, Name, CaseInsensitive)) return TRUE;

        Cursor += (Unsecure.Length / sizeof(WCHAR)) + 1;
    }

    return FALSE;
}

/* THE NAME WALK **************************************************************/

typedef struct _OBP_WALK
{
    POBJECT_DIRECTORY Directory;
    POBJECT_DIRECTORY Held;
    POBJECT_DIRECTORY HeldParent;
    PVOID ParseRoot;
    UNICODE_STRING Remaining;
    ULONG Reparse;
} OBP_WALK, *POBP_WALK;

static
VOID
ObpEndWalk(
    _Inout_ POBP_WALK Walk)
{
    ObpHoldDirectory(&Walk->Held, NULL);
    ObpHoldDirectory(&Walk->HeldParent, NULL);

    if (Walk->ParseRoot != NULL)
    {
        ObDereferenceObject(Walk->ParseRoot);
        Walk->ParseRoot = NULL;
    }
}

/*
 * Settles where a lookup starts: a root handle names its own directory, and
 * without one the path has to be absolute.
 */
static
NTSTATUS
ObpBeginWalk(
    _In_opt_ HANDLE RootHandle,
    _In_ PUNICODE_STRING ObjectName,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_ POBP_WALK Walk)
{
    POBJECT_DIRECTORY Root;
    NTSTATUS Status;

    Walk->Remaining = *ObjectName;

    if (RootHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(RootHandle,
                                           0,
                                           ObpDirectoryObjectType,
                                           AccessMode,
                                           (PVOID *)&Root,
                                           NULL);
        if (Status == STATUS_OBJECT_TYPE_MISMATCH)
        {
            /* Not a directory, so the object parses the rest of the path */
            Status = ObReferenceObjectByHandle(RootHandle,
                                               0,
                                               NULL,
                                               AccessMode,
                                               &Walk->ParseRoot,
                                               NULL);
            return Status;
        }

        if (!NT_SUCCESS(Status)) return Status;

        /* The reference we were given is the one the walk holds */
        Walk->Held = Root;
        Walk->Directory = Root;

        /* A path under a root directory must be relative to it */
        if (Walk->Remaining.Length != 0 &&
            Walk->Remaining.Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
        {
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        }

        return STATUS_SUCCESS;
    }

    if (Walk->Remaining.Length == 0) return STATUS_OBJECT_NAME_INVALID;
    if (Walk->Remaining.Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
        return STATUS_OBJECT_PATH_SYNTAX_BAD;

    Status = ObpRedirectDosDevices(&Walk->Remaining, &Walk->Directory);
    if (NT_SUCCESS(Status))
    {
        Walk->Held = Walk->Directory;
        return STATUS_SUCCESS;
    }

    Walk->Directory = ObpRootDirectoryObject;
    return STATUS_SUCCESS;
}

/* Sends the walk back to the root with whatever the parse routine left in the
 * complete name. */
static
NTSTATUS
ObpRestartWalk(
    _Inout_ POBP_WALK Walk,
    _In_ PUNICODE_STRING ObjectName)
{
    if (Walk->Reparse == 0) return STATUS_OBJECT_NAME_NOT_FOUND;
    Walk->Reparse--;

    if (ObjectName->Length == 0 ||
        ObjectName->Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    ObpHoldDirectory(&Walk->HeldParent, NULL);
    ObpHoldDirectory(&Walk->Held, NULL);

    if (Walk->ParseRoot != NULL)
    {
        ObDereferenceObject(Walk->ParseRoot);
        Walk->ParseRoot = NULL;
    }

    return ObpBeginWalk(NULL, ObjectName, KernelMode, Walk);
}

/*
 * Calls a type's parse routine with the directory lock and the walk's own
 * directory references let go, since the routine may block or reparse.
 */
static
NTSTATUS
ObpParseAt(
    _Inout_ POBP_WALK Walk,
    _In_ PVOID Object,
    _In_ POBJECT_TYPE FoundType,
    _In_ POBJECT_TYPE ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE CheckMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING ObjectName,
    _Inout_opt_ PVOID ParseContext,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Inout_ POBP_LOOKUP_CONTEXT LookupContext,
    _Out_ PVOID *FoundObject)
{
    NTSTATUS Status;
    KIRQL CalloutIrql;

    ObReferenceObject(Object);
    ObpReleaseLookupContext(LookupContext);
    ObpHoldDirectory(&Walk->HeldParent, NULL);
    ObpHoldDirectory(&Walk->Held, NULL);
    Walk->Directory = NULL;

    ObpCalloutStart(&CalloutIrql);
    Status = ObpCallParseProcedure(FoundType,
                                   Object,
                                   ObjectType,
                                   AccessState,
                                   CheckMode,
                                   Attributes,
                                   ObjectName,
                                   &Walk->Remaining,
                                   ParseContext,
                                   SecurityQos,
                                   FoundObject);
    ObpCalloutEnd(CalloutIrql, "Parse", FoundType, Object);

    if (*FoundObject != Object) ObDereferenceObject(Object);
    return Status;
}

/*
 * A session directory only accepts a section or a symbolic link from the
 * session that owns it, from a caller holding the privilege, or under a name
 * the configuration marks unsecure.
 */
static
BOOLEAN
ObpMayInsertGlobalName(
    _In_ POBJECT_DIRECTORY Directory,
    _In_ PVOID InsertObject,
    _In_ PUNICODE_STRING Component,
    _In_ ULONG Attributes,
    _In_ KPROCESSOR_MODE CheckMode)
{
    POBJECT_TYPE Type;

    if (Directory->SessionId == MAXULONG) return TRUE;
    if (Directory->SessionId == PsGetCurrentProcessSessionId()) return TRUE;

    Type = ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(InsertObject));
    if (Type != MmSectionObjectType && Type != ObpSymbolicLinkObjectType)
        return TRUE;

    if (SeSinglePrivilegeCheck(SeCreateGlobalPrivilege, CheckMode)) return TRUE;

    return ObpIsUnsecureName(Component,
                             BooleanFlagOn(Attributes, OBJ_CASE_INSENSITIVE));
}

/*
 * Replaces the whole path the object was created with by just the component
 * that names it. The component still lives in the old buffer while it is
 * copied, so the copy comes first and the old buffer goes afterwards.
 */
static
NTSTATUS
ObpNameInsertedObject(
    _In_ POBJECT_HEADER Header,
    _In_ PUNICODE_STRING Component)
{
    POBJECT_HEADER_NAME_INFO NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);
    PWCHAR Buffer;

    if (NameInfo == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Buffer = ExAllocatePoolWithTag(PagedPool, Component->Length, OB_NAME_TAG);
    if (Buffer == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Buffer, Component->Buffer, Component->Length);

    if (NameInfo->Name.Buffer != NULL)
        ExFreePoolWithTag(NameInfo->Name.Buffer, OB_NAME_TAG);

    NameInfo->Name.Buffer = Buffer;
    NameInfo->Name.Length = Component->Length;
    NameInfo->Name.MaximumLength = Component->Length;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
ObpLookupObjectName(
    _In_opt_ HANDLE RootHandle,
    _Inout_ PUNICODE_STRING ObjectName,
    _In_ ULONG Attributes,
    _In_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PVOID ParseContext,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _In_opt_ PVOID InsertObject,
    _Inout_ PACCESS_STATE AccessState,
    _Out_ POBP_LOOKUP_CONTEXT LookupContext,
    _Out_ PVOID *FoundObject)
{
    KPROCESSOR_MODE CheckMode = AccessMode;
    UNICODE_STRING Component;
    POBJECT_HEADER Header;
    POBJECT_TYPE FoundType;
    NTSTATUS Status, AccessStatus;
    OBP_WALK Walk;
    PVOID Object;

    PAGED_CODE();

    RtlInitEmptyUnicodeString(&Component, NULL, 0);
    *FoundObject = NULL;
    ObpInitializeLookupContext(LookupContext);

    RtlZeroMemory(&Walk, sizeof(Walk));
    Walk.Reparse = OBP_MAX_REPARSE;

    /* An insertion is the kernel naming an object it already owns */
    if (InsertObject != NULL) CheckMode = KernelMode;

    /* A lone separator names the root itself, which hangs from nothing: the
     * first directory created under that name becomes the root */
    if (RootHandle == NULL &&
        ObjectName->Length == sizeof(WCHAR) &&
        ObjectName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
    {
        if (InsertObject != NULL)
        {
            if (ObpRootDirectoryObject != NULL)
                return STATUS_OBJECT_NAME_COLLISION;

            ObpRootDirectoryObject = InsertObject;
            ObReferenceObject(InsertObject);
            *FoundObject = InsertObject;
            return STATUS_SUCCESS;
        }

        if (ObpRootDirectoryObject == NULL) return STATUS_OBJECT_PATH_NOT_FOUND;

        ObReferenceObject(ObpRootDirectoryObject);
        *FoundObject = ObpRootDirectoryObject;
        return STATUS_SUCCESS;
    }

    Status = ObpBeginWalk(RootHandle, ObjectName, AccessMode, &Walk);

    /* A root handle that is not a directory names an object whose own parse
     * routine takes the whole of the rest of the path */
    if (NT_SUCCESS(Status) && Walk.ParseRoot != NULL)
    {
        Object = Walk.ParseRoot;
        FoundType = ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(Object));

        if (FoundType->TypeInfo.ParseProcedure == NULL &&
            FoundType->TypeInfo.ParseProcedureEx == NULL)
        {
            ObpEndWalk(&Walk);
            return STATUS_OBJECT_TYPE_MISMATCH;
        }

        Status = ObpParseAt(&Walk, Object, FoundType, ObjectType, AccessState,
                            CheckMode, Attributes, ObjectName, ParseContext,
                            SecurityQos, LookupContext, FoundObject);

        if (Status == STATUS_REPARSE) Status = ObpRestartWalk(&Walk, ObjectName);
        else if (Status == STATUS_REPARSE_OBJECT && *FoundObject == NULL)
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        else if (NT_SUCCESS(Status) && *FoundObject == NULL)
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        else
        {
            if (!NT_SUCCESS(Status)) *FoundObject = NULL;
            ObpEndWalk(&Walk);
            return Status;
        }
    }

    /* A path that names the directory the walk starts in, and nothing under
     * it, is answered by that directory: "\??" is the common one */
    if (NT_SUCCESS(Status) && Walk.ParseRoot == NULL && InsertObject == NULL &&
        (Walk.Remaining.Length == 0 ||
         (Walk.Remaining.Length == sizeof(WCHAR) &&
          Walk.Remaining.Buffer[0] == OBJ_NAME_PATH_SEPARATOR)))
    {
        ObReferenceObject(Walk.Directory);
        *FoundObject = Walk.Directory;
        ObpEndWalk(&Walk);
        return STATUS_SUCCESS;
    }

    while (NT_SUCCESS(Status))
    {
        if (!ObpTakeComponent(&Walk.Remaining, &Component) ||
            Component.Length == 0)
        {
            /* The path ran out, or ended on a separator, so it names nothing */
            Status = STATUS_OBJECT_NAME_INVALID;
            break;
        }

        /* The last component of an insertion needs the directory to itself */
        if (InsertObject != NULL && Walk.Remaining.Length == 0)
        {
            ObpReleaseLookupContext(LookupContext);
            ObpAcquireLookupContextLock(LookupContext, Walk.Directory);
        }

        Object = ObpLookupEntryDirectory(Walk.Directory,
                                         &Component,
                                         Attributes,
                                         (InsertObject == NULL),
                                         LookupContext);
        if (Object == NULL)
        {
            if (Walk.Remaining.Length != 0)
            {
                Status = STATUS_OBJECT_PATH_NOT_FOUND;
                break;
            }

            if (InsertObject == NULL)
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                break;
            }

            if (!ObpMayInsertGlobalName(Walk.Directory, InsertObject,
                                        &Component, Attributes, AccessMode))
            {
                Status = STATUS_ACCESS_DENIED;
                break;
            }

            Header = OBJECT_TO_OBJECT_HEADER(InsertObject);

            /* Name it here and leave the directory locked, so the caller can
             * undo the insertion under the same lock if what follows fails */
            if (!ObpInsertEntryDirectory(Walk.Directory, LookupContext, Header))
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            Status = ObpNameInsertedObject(Header, &Component);
            if (!NT_SUCCESS(Status))
            {
                ObpDeleteEntryDirectory(LookupContext);
                break;
            }

            /* From now on the name holds a reference on the object and on the
             * directory it hangs in, which the name info releases */
            ObReferenceObject(InsertObject);
            ObReferenceObject(Walk.Directory);
            *FoundObject = InsertObject;
            break;
        }

        Header = OBJECT_TO_OBJECT_HEADER(Object);
        FoundType = ObpGetObjectTypeFromHeader(Header);

        if ((FoundType->TypeInfo.ParseProcedure != NULL ||
             FoundType->TypeInfo.ParseProcedureEx != NULL) &&
            (InsertObject == NULL || FoundType == ObpSymbolicLinkObjectType))
        {
            Status = ObpParseAt(&Walk, Object, FoundType, ObjectType,
                                AccessState, CheckMode, Attributes, ObjectName,
                                ParseContext, SecurityQos, LookupContext,
                                FoundObject);

            if (Status == STATUS_REPARSE)
            {
                Status = ObpRestartWalk(&Walk, ObjectName);
                continue;
            }

            if (Status == STATUS_REPARSE_OBJECT || NT_SUCCESS(Status))
            {
                if (*FoundObject == NULL) Status = STATUS_OBJECT_NAME_NOT_FOUND;
            }

            break;
        }

        if (Walk.Remaining.Length == 0)
        {
            /* The search already parked one reference in the context, so this
             * is the one the caller takes away */
            ObReferenceObject(Object);
            *FoundObject = Object;
            break;
        }

        if (FoundType != ObpDirectoryObjectType)
        {
            Status = STATUS_OBJECT_PATH_NOT_FOUND;
            break;
        }

        /* Walking through a directory needs the right to traverse it */
        if (CheckMode != KernelMode &&
            !ObpCheckTraverseAccess(Object,
                                    DIRECTORY_TRAVERSE,
                                    AccessState,
                                    TRUE,
                                    CheckMode,
                                    &AccessStatus))
        {
            Status = AccessStatus;
            break;
        }

        ObpHoldDirectory(&Walk.HeldParent, Walk.Directory);
        ObpHoldDirectory(&Walk.Held, Object);
        Walk.Directory = Object;
    }

    if (!NT_SUCCESS(Status))
    {
        ObpReleaseLookupContext(LookupContext);
        *FoundObject = NULL;
    }

    ObpEndWalk(&Walk);
    return Status;
}

/* QUERYING A NAME ************************************************************/

/* The path of an object is its own name with the name of every directory
 * above it, up to but not including the root. */
static
ULONG
ObpPathLength(
    _In_ POBJECT_HEADER Header)
{
    POBJECT_HEADER_NAME_INFO NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);
    POBJECT_DIRECTORY Directory;
    ULONG Bytes = 0;

    while (NameInfo != NULL && NameInfo->Directory != NULL)
    {
        Bytes += sizeof(OBJ_NAME_PATH_SEPARATOR) + NameInfo->Name.Length;

        Directory = NameInfo->Directory;
        if (Directory == ObpRootDirectoryObject) break;

        NameInfo = OBJECT_HEADER_TO_NAME_INFO(OBJECT_TO_OBJECT_HEADER(Directory));
    }

    return Bytes;
}

/* Writes the same path backwards from the end of the caller's buffer. */
static
VOID
ObpWritePath(
    _In_ POBJECT_HEADER Header,
    _In_ PUCHAR End)
{
    POBJECT_HEADER_NAME_INFO NameInfo = OBJECT_HEADER_TO_NAME_INFO(Header);
    POBJECT_DIRECTORY Directory;
    PUCHAR Cursor = End;

    while (NameInfo != NULL && NameInfo->Directory != NULL)
    {
        Cursor -= NameInfo->Name.Length;
        RtlCopyMemory(Cursor, NameInfo->Name.Buffer, NameInfo->Name.Length);

        Cursor -= sizeof(OBJ_NAME_PATH_SEPARATOR);
        *(PWCHAR)Cursor = OBJ_NAME_PATH_SEPARATOR;

        Directory = NameInfo->Directory;
        if (Directory == ObpRootDirectoryObject) break;

        NameInfo = OBJECT_HEADER_TO_NAME_INFO(OBJECT_TO_OBJECT_HEADER(Directory));
    }
}

NTSTATUS
NTAPI
ObQueryNameString(
    _In_ PVOID Object,
    _Out_opt_ POBJECT_NAME_INFORMATION ObjectNameInfo,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    POBJECT_HEADER Header = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_TYPE Type = ObpGetObjectTypeFromHeader(Header);
    POBJECT_HEADER_NAME_INFO NameInfo;
    ULONG PathBytes, Needed;
    NTSTATUS Status;

    PAGED_CODE();

    NameInfo = ObpReferenceNameInfo(Header);

    /* A type that names its own objects answers for them */
    if (Type->TypeInfo.QueryNameProcedure != NULL)
    {
        Status = Type->TypeInfo.QueryNameProcedure(Object,
                                                   (NameInfo != NULL),
                                                   ObjectNameInfo,
                                                   Length,
                                                   ReturnLength,
                                                   KernelMode);
        ObpDereferenceNameInfo(NameInfo);
        return Status;
    }

    PathBytes = ObpPathLength(Header);
    Needed = sizeof(OBJECT_NAME_INFORMATION) + PathBytes;
    if (PathBytes != 0) Needed += sizeof(UNICODE_NULL);

    *ReturnLength = Needed;

    if (Length < Needed)
    {
        ObpDereferenceNameInfo(NameInfo);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PathBytes == 0)
    {
        /* An object outside the namespace reports an empty name */
        RtlInitEmptyUnicodeString(&ObjectNameInfo->Name, NULL, 0);
        ObpDereferenceNameInfo(NameInfo);
        return STATUS_SUCCESS;
    }

    ObpWritePath(Header, (PUCHAR)ObjectNameInfo + Needed - sizeof(UNICODE_NULL));

    ObjectNameInfo->Name.Buffer = (PWCHAR)((PUCHAR)ObjectNameInfo +
                                           sizeof(OBJECT_NAME_INFORMATION));
    ObjectNameInfo->Name.Length = (USHORT)PathBytes;
    ObjectNameInfo->Name.MaximumLength = (USHORT)(PathBytes + sizeof(UNICODE_NULL));
    ObjectNameInfo->Name.Buffer[PathBytes / sizeof(WCHAR)] = UNICODE_NULL;

    ObpDereferenceNameInfo(NameInfo);
    return STATUS_SUCCESS;
}

/* INITIALISATION *************************************************************/

CODE_SEG("INIT")
static
NTSTATUS
ObpGetDosDevicesProtection(
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor)
{
    PSECURITY_DESCRIPTOR Sd;
    NTSTATUS Status;
    ULONG AclSize;
    PACL Dacl;

    /* The hardened layout keeps the global namespace writable only by the
     * system, with the inheritable rights every session directory starts from */
    if (ObpProtectionMode != 0)
    {
        AclSize = sizeof(ACL) +
                  sizeof(ACE) + RtlLengthSid(SeWorldSid) +
                  sizeof(ACE) + RtlLengthSid(SeLocalSystemSid) +
                  sizeof(ACE) + RtlLengthSid(SeWorldSid) +
                  sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid) +
                  sizeof(ACE) + RtlLengthSid(SeLocalSystemSid) +
                  sizeof(ACE) + RtlLengthSid(SeCreatorOwnerSid);
    }
    else
    {
        AclSize = sizeof(ACL) +
                  sizeof(ACE) + RtlLengthSid(SeWorldSid) +
                  sizeof(ACE) + RtlLengthSid(SeLocalSystemSid) +
                  sizeof(ACE) + RtlLengthSid(SeWorldSid);
    }

    Sd = ExAllocatePoolWithTag(PagedPool,
                               sizeof(SECURITY_DESCRIPTOR) + AclSize,
                               TAG_SD);
    if (Sd == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Status = RtlCreateSecurityDescriptor(Sd, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Sd, TAG_SD);
        return Status;
    }

    Dacl = (PACL)((PUCHAR)Sd + sizeof(SECURITY_DESCRIPTOR));
    Status = RtlCreateAcl(Dacl, AclSize, ACL_REVISION);

    if (NT_SUCCESS(Status) && ObpProtectionMode != 0)
    {
        RtlAddAccessAllowedAce(Dacl, ACL_REVISION,
                               GENERIC_READ | GENERIC_EXECUTE, SeWorldSid);
        RtlAddAccessAllowedAce(Dacl, ACL_REVISION, GENERIC_ALL,
                               SeLocalSystemSid);
        RtlAddAccessAllowedAceEx(Dacl, ACL_REVISION,
                                 INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE |
                                 OBJECT_INHERIT_ACE,
                                 GENERIC_EXECUTE, SeWorldSid);
        RtlAddAccessAllowedAceEx(Dacl, ACL_REVISION,
                                 INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE |
                                 OBJECT_INHERIT_ACE,
                                 GENERIC_ALL, SeAliasAdminsSid);
        RtlAddAccessAllowedAceEx(Dacl, ACL_REVISION,
                                 INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE |
                                 OBJECT_INHERIT_ACE,
                                 GENERIC_ALL, SeLocalSystemSid);
        RtlAddAccessAllowedAceEx(Dacl, ACL_REVISION,
                                 INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE |
                                 OBJECT_INHERIT_ACE,
                                 GENERIC_ALL, SeCreatorOwnerSid);
    }
    else if (NT_SUCCESS(Status))
    {
        RtlAddAccessAllowedAce(Dacl, ACL_REVISION,
                               GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE,
                               SeWorldSid);
        RtlAddAccessAllowedAce(Dacl, ACL_REVISION, GENERIC_ALL,
                               SeLocalSystemSid);
        RtlAddAccessAllowedAceEx(Dacl, ACL_REVISION,
                                 INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE |
                                 OBJECT_INHERIT_ACE,
                                 GENERIC_ALL, SeWorldSid);
    }

    if (NT_SUCCESS(Status))
        Status = RtlSetDaclSecurityDescriptor(Sd, TRUE, Dacl, FALSE);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Sd, TAG_SD);
        return Status;
    }

    *SecurityDescriptor = Sd;
    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
static
NTSTATUS
ObpCreatePermanentLink(
    _In_ HANDLE RootDirectory,
    _In_ PCWSTR Name,
    _In_ PCWSTR Target)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING LinkName, TargetName;
    NTSTATUS Status;
    HANDLE Handle;

    RtlInitUnicodeString(&LinkName, Name);
    RtlInitUnicodeString(&TargetName, Target);
    InitializeObjectAttributes(&ObjectAttributes,
                               &LinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               RootDirectory,
                               NULL);

    Status = NtCreateSymbolicLinkObject(&Handle,
                                        SYMBOLIC_LINK_ALL_ACCESS,
                                        &ObjectAttributes,
                                        &TargetName);
    if (NT_SUCCESS(Status)) NtClose(Handle);

    return Status;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
ObpCreateDosDevicesDirectory(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    PSECURITY_DESCRIPTOR Sd;
    UNICODE_STRING Name;
    NTSTATUS Status;
    HANDLE Handle;

    /* Per-session DOS devices need both a hardened namespace and the
     * configuration's consent */
    ObpLUIDDeviceMapsEnabled =
        (ObpProtectionMode == 0 || ObpLUIDDeviceMapsDisabled != 0) ? 0 : 1;

    Status = ObpGetDosDevicesProtection(&Sd);
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&Name, L"\\GLOBAL??");
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               Sd);

    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Sd, TAG_SD);
        return Status;
    }

    /* This directory is the system device map every other one falls back on */
    Status = ObSetDeviceMap(NULL, Handle);

    if (NT_SUCCESS(Status))
        Status = ObpCreatePermanentLink(Handle, L"GLOBALROOT", L"");

    if (NT_SUCCESS(Status))
        Status = ObpCreatePermanentLink(Handle, L"Global", L"\\GLOBAL??");

    NtClose(Handle);

    if (NT_SUCCESS(Status))
        Status = ObpCreatePermanentLink(NULL, L"\\DosDevices", L"\\??");

    ExFreePoolWithTag(Sd, TAG_SD);
    return Status;
}

/* EOF */
