/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object manager namespace
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_DIRECTORY ObpRootDirectoryObject;
POBJECT_DIRECTORY ObpTypeDirectoryObject;

ALIGNEDNAME ObpDosDevicesShortNamePrefix = {{L'\\', L'?', L'?', L'\\'}};
ALIGNEDNAME ObpDosDevicesShortNameRoot = {{L'\\', L'?', L'?', L'\0'}};

UNICODE_STRING ObpDosDevicesShortName =
{
    sizeof(ObpDosDevicesShortNamePrefix),
    sizeof(ObpDosDevicesShortNamePrefix),
    (PWSTR)&ObpDosDevicesShortNamePrefix
};

ULONG ObpCaseInsensitive = TRUE;

WCHAR ObpUnsecureGlobalNamesBuffer[128] = {0};
ULONG ObpUnsecureGlobalNamesLength = sizeof(ObpUnsecureGlobalNamesBuffer);

#define OBP_MAX_LOOKUP_REPARSE 30

/* INITIALISATION *************************************************************/

/*
 * \GLOBAL?? is readable by everyone; the protection mode decides whether a
 * plain user may also create names in it.
 */
CODE_SEG("INIT")
NTSTATUS
NTAPI
ObpGetDosDevicesProtection(
    _Out_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    NTSTATUS Status;
    ULONG AclSize;
    PACL Dacl;

    Status = RtlCreateSecurityDescriptor(SecurityDescriptor,
                                         SECURITY_DESCRIPTOR_REVISION);
    ASSERT(NT_SUCCESS(Status));

    if (ObpProtectionMode & 1)
    {
        AclSize = sizeof(ACL) +
                  sizeof(ACE) + RtlLengthSid(SeWorldSid) +
                  sizeof(ACE) + RtlLengthSid(SeLocalSystemSid) +
                  sizeof(ACE) + RtlLengthSid(SeWorldSid) +
                  sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid) +
                  sizeof(ACE) + RtlLengthSid(SeLocalSystemSid) +
                  sizeof(ACE) + RtlLengthSid(SeCreatorOwnerSid);

        Dacl = ExAllocatePoolWithTag(PagedPool, AclSize, TAG_DACL);
        if (Dacl == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Status = RtlCreateAcl(Dacl, AclSize, ACL_REVISION);
        ASSERT(NT_SUCCESS(Status));

        Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION,
                                        GENERIC_READ | GENERIC_EXECUTE,
                                        SeWorldSid);
        ASSERT(NT_SUCCESS(Status));
        Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, GENERIC_ALL,
                                        SeLocalSystemSid);
        ASSERT(NT_SUCCESS(Status));
        Status = RtlAddAccessAllowedAceEx(Dacl, ACL_REVISION,
                                          INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE,
                                          GENERIC_EXECUTE, SeWorldSid);
        ASSERT(NT_SUCCESS(Status));
        Status = RtlAddAccessAllowedAceEx(Dacl, ACL_REVISION,
                                          INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE,
                                          GENERIC_ALL, SeAliasAdminsSid);
        ASSERT(NT_SUCCESS(Status));
        Status = RtlAddAccessAllowedAceEx(Dacl, ACL_REVISION,
                                          INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE,
                                          GENERIC_ALL, SeLocalSystemSid);
        ASSERT(NT_SUCCESS(Status));
        Status = RtlAddAccessAllowedAceEx(Dacl, ACL_REVISION,
                                          INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE,
                                          GENERIC_ALL, SeCreatorOwnerSid);
        ASSERT(NT_SUCCESS(Status));
    }
    else
    {
        AclSize = sizeof(ACL) +
                  sizeof(ACE) + RtlLengthSid(SeLocalSystemSid) +
                  sizeof(ACE) + RtlLengthSid(SeWorldSid) +
                  sizeof(ACE) + RtlLengthSid(SeLocalSystemSid);

        Dacl = ExAllocatePoolWithTag(PagedPool, AclSize, TAG_DACL);
        if (Dacl == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        Status = RtlCreateAcl(Dacl, AclSize, ACL_REVISION);
        ASSERT(NT_SUCCESS(Status));

        Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION,
                                        GENERIC_READ | GENERIC_EXECUTE | GENERIC_WRITE,
                                        SeWorldSid);
        ASSERT(NT_SUCCESS(Status));
        Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, GENERIC_ALL,
                                        SeLocalSystemSid);
        ASSERT(NT_SUCCESS(Status));
        Status = RtlAddAccessAllowedAceEx(Dacl, ACL_REVISION,
                                          INHERIT_ONLY_ACE | CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE,
                                          GENERIC_ALL, SeWorldSid);
        ASSERT(NT_SUCCESS(Status));
    }

    Status = RtlSetDaclSecurityDescriptor(SecurityDescriptor, TRUE, Dacl, FALSE);
    ASSERT(NT_SUCCESS(Status));

    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
NTAPI
ObpFreeDosDevicesProtection(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    BOOLEAN DaclPresent, DaclDefaulted;
    NTSTATUS Status;
    PACL Dacl;

    Status = RtlGetDaclSecurityDescriptor(SecurityDescriptor,
                                          &DaclPresent,
                                          &Dacl,
                                          &DaclDefaulted);
    ASSERT(NT_SUCCESS(Status));
    ASSERT(DaclPresent);
    ASSERT(Dacl != NULL);

    ExFreePoolWithTag(Dacl, TAG_DACL);
}

static
CODE_SEG("INIT")
VOID
NTAPI
ObpCreatePermanentLink(
    _In_ PCWSTR LinkName,
    _In_opt_ HANDLE RootHandle,
    _In_ PUNICODE_STRING Target,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _Inout_ PNTSTATUS Status)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    HANDLE SymHandle;

    RtlInitUnicodeString(&Name, LinkName);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_PERMANENT,
                               RootHandle,
                               SecurityDescriptor);

    *Status = NtCreateSymbolicLinkObject(&SymHandle,
                                         SYMBOLIC_LINK_ALL_ACCESS,
                                         &ObjectAttributes,
                                         Target);
    if (NT_SUCCESS(*Status)) NtClose(SymHandle);
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
ObpCreateDosDevicesDirectory(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING RootName, TargetName;
    SECURITY_DESCRIPTOR DosDevicesSD;
    NTSTATUS Status;
    HANDLE Handle;

    /* LUID mapped devices only make sense when the names are protected */
    if (ObpProtectionMode == 0 || ObpLUIDDeviceMapsDisabled != 0)
        ObpLUIDDeviceMapsEnabled = 0;
    else
        ObpLUIDDeviceMapsEnabled = 1;

    Status = ObpGetDosDevicesProtection(&DosDevicesSD);
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitUnicodeString(&RootName, L"\\GLOBAL??");
    InitializeObjectAttributes(&ObjectAttributes,
                               &RootName,
                               OBJ_PERMANENT,
                               NULL,
                               &DosDevicesSD);

    Status = NtCreateDirectoryObject(&Handle, DIRECTORY_ALL_ACCESS, &ObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        Status = ObSetDeviceMap(NULL, Handle);
        if (NT_SUCCESS(Status))
        {
            /* GLOBALROOT reaches the namespace root, Global the global names */
            RtlInitUnicodeString(&TargetName, L"");
            ObpCreatePermanentLink(L"GLOBALROOT", Handle, &TargetName,
                                   &DosDevicesSD, &Status);

            ObpCreatePermanentLink(L"Global", Handle, &RootName,
                                   &DosDevicesSD, &Status);
        }

        NtClose(Handle);

        if (NT_SUCCESS(Status))
        {
            RtlInitUnicodeString(&RootName, (PCWSTR)&ObpDosDevicesShortNameRoot);
            ObpCreatePermanentLink(L"\\DosDevices", NULL, &RootName,
                                   &DosDevicesSD, &Status);
        }
    }

    ObpFreeDosDevicesProtection(&DosDevicesSD);
    return Status;
}

/* PRIVATE FUNCTIONS **********************************************************/

VOID
NTAPI
ObpDeleteNameCheck(
    _In_ PVOID Object)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_HEADER_NAME_INFO ObjectNameInfo;
    POBJECT_TYPE ObjectType;
    OBP_LOOKUP_CONTEXT Context;
    PVOID Directory = NULL;

    ObjectNameInfo = ObpReferenceNameInfo(ObjectHeader);
    ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);

    /* A permanent object keeps its name even with no handle left */
    if (ObjectHeader->HandleCount != 0 ||
        ObjectNameInfo == NULL ||
        ObjectNameInfo->Name.Length == 0 ||
        ObjectNameInfo->Directory == NULL ||
        (ObjectHeader->Flags & OB_FLAG_PERMANENT))
    {
        ObpDereferenceNameInfo(ObjectNameInfo);
        return;
    }

    ObpInitializeLookupContext(&Context);
    ObpAcquireLookupContextLock(&Context, ObjectNameInfo->Directory);

    Object = ObpLookupEntryDirectory(ObjectNameInfo->Directory,
                                     &ObjectNameInfo->Name,
                                     0,
                                     FALSE,
                                     &Context);
    if (Object != NULL)
    {
        ObpAcquireObjectLock(ObjectHeader);

        /* Check again now that nothing else can take a handle */
        if (ObjectHeader->HandleCount == 0 &&
            !(ObjectHeader->Flags & OB_FLAG_PERMANENT))
        {
            ObpDeleteEntryDirectory(&Context);

            if (ObjectType == ObpSymbolicLinkObjectType)
                ObpDeleteSymbolicLinkName(Object);

            ObjectNameInfo = OBJECT_HEADER_TO_NAME_INFO(ObjectHeader);
            Directory = ObjectNameInfo->Directory;
        }

        ObpReleaseObjectLock(ObjectHeader);
    }

    ObpReleaseLookupContext(&Context);
    ObpDereferenceNameInfo(ObjectNameInfo);

    /* The name is gone, so the reference it held goes with it */
    if (Directory != NULL)
    {
        ObpDereferenceNameInfo(ObjectNameInfo);
        ObDereferenceObject(Object);
    }
}

BOOLEAN
NTAPI
ObpIsUnsecureName(
    _In_ PUNICODE_STRING ObjectName,
    _In_ BOOLEAN CaseInSensitive)
{
    UNICODE_STRING UnsecureName;
    PWSTR UnsecureBuffer;

    if (ObpUnsecureGlobalNamesBuffer[0] == UNICODE_NULL) return FALSE;

    /* The registry value holds a multi-string of name prefixes */
    UnsecureBuffer = &ObpUnsecureGlobalNamesBuffer[0];
    for (;;)
    {
        RtlInitUnicodeString(&UnsecureName, UnsecureBuffer);
        if (UnsecureName.Length == 0) return FALSE;

        if (RtlPrefixUnicodeString(&UnsecureName, ObjectName, CaseInSensitive))
            return TRUE;

        UnsecureBuffer = (PWSTR)((ULONG_PTR)UnsecureBuffer +
                                 UnsecureName.Length + sizeof(UNICODE_NULL));
    }
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
        return ParseObjectType->TypeInfo.ParseProcedure(ParseObject, ObjectType,
                                                        AccessState, AccessMode,
                                                        Attributes, CompleteName,
                                                        RemainingName, ParseContext,
                                                        SecurityQos, Object);
    }

    RtlZeroMemory(&ExtendedParameters, sizeof(ExtendedParameters));
    ExtendedParameters.Length = sizeof(ExtendedParameters);
    ExtendedParameters.RestrictedAccessMask = MAXULONG;

    return ParseObjectType->TypeInfo.ParseProcedureEx(ParseObject, ObjectType,
                                                      AccessState, AccessMode,
                                                      Attributes, CompleteName,
                                                      RemainingName, ParseContext,
                                                      SecurityQos,
                                                      &ExtendedParameters, Object);
}

/* The state the namespace walk carries from one component to the next */
typedef struct _OBP_LOOKUP_STATE
{
    POBJECT_DIRECTORY RootDirectory;
    POBJECT_DIRECTORY Directory;
    POBJECT_DIRECTORY ParentDirectory;
    POBJECT_DIRECTORY ReferencedDirectory;
    POBJECT_DIRECTORY ReferencedParentDirectory;
    PDEVICE_MAP DeviceMap;
    UNICODE_STRING LocalName;
    HANDLE RootHandle;
    ULONG MaxReparse;
    BOOLEAN SymLink;
} OBP_LOOKUP_STATE, *POBP_LOOKUP_STATE;

/*
 * Re-anchors the walk at the namespace root, following the caller's own DOS
 * devices directory when the path starts with \??. Returns FALSE when the path
 * named the DOS devices directory itself and the lookup is already finished.
 */
static
BOOLEAN
ObpPrepareParseFromRoot(
    _Inout_ POBP_LOOKUP_STATE State,
    _In_ PUNICODE_STRING ObjectName,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PVOID *FoundObject,
    _Out_ PNTSTATUS Status)
{
    State->LocalName = *ObjectName;

    if (State->DeviceMap != NULL)
    {
        ObfDereferenceDeviceMap(State->DeviceMap);
        State->DeviceMap = NULL;
    }

    /* The prefix is compared as one aligned quadword, so only try it when the
     * name is aligned for that */
    if ((ULONG_PTR)ObjectName->Buffer & 7) return TRUE;

    if (ObjectName->Length >= ObpDosDevicesShortName.Length &&
        *(PULONGLONG)ObjectName->Buffer == ObpDosDevicesShortNamePrefix.Alignment.QuadPart)
    {
        State->DeviceMap = ObpReferenceDeviceMap();
        if (State->DeviceMap != NULL && State->DeviceMap->DosDevicesDirectory != NULL)
        {
            State->LocalName.Length -= ObpDosDevicesShortName.Length;
            State->LocalName.MaximumLength -= ObpDosDevicesShortName.Length;
            State->LocalName.Buffer += (ObpDosDevicesShortName.Length / sizeof(WCHAR));
            State->Directory = State->DeviceMap->DosDevicesDirectory;
        }
        return TRUE;
    }

    /* The bare name \?? opens that directory */
    if (ObjectName->Length == ObpDosDevicesShortName.Length - sizeof(WCHAR) &&
        *(PULONG)ObjectName->Buffer == ObpDosDevicesShortNameRoot.Alignment.LowPart &&
        *((PWCHAR)ObjectName->Buffer + 2) == (WCHAR)ObpDosDevicesShortNameRoot.Alignment.HighPart)
    {
        State->DeviceMap = ObpReferenceDeviceMap();
        if (State->DeviceMap != NULL && State->DeviceMap->DosDevicesDirectory != NULL)
        {
            *Status = ObReferenceObjectByPointer(State->DeviceMap->DosDevicesDirectory,
                                                 0,
                                                 ObjectType,
                                                 AccessMode);
            if (NT_SUCCESS(*Status)) *FoundObject = State->DeviceMap->DosDevicesDirectory;

            ObfDereferenceDeviceMap(State->DeviceMap);
            State->DeviceMap = NULL;
            return FALSE;
        }
    }

    return TRUE;
}

/*
 * Runs the parse procedure of a root handle that is not a directory. Returns
 * TRUE when the caller has to restart the lookup from the namespace root.
 */
static
BOOLEAN
ObpParseRootHandleObject(
    _Inout_ POBP_LOOKUP_STATE State,
    _Inout_ PUNICODE_STRING ObjectName,
    _In_opt_ POBJECT_TYPE ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessCheckMode,
    _In_ ULONG Attributes,
    _Inout_opt_ PVOID ParseContext,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Out_ PVOID *FoundObject,
    _Out_ PNTSTATUS Status)
{
    POBJECT_TYPE ParseObjectType;
    UNICODE_STRING RemainingName;
    ULONG MaxReparse = OBP_MAX_LOOKUP_REPARSE;
    KIRQL CalloutIrql;
    PVOID Object;

    ParseObjectType = ObpGetObjectTypeFromHeader(OBJECT_TO_OBJECT_HEADER(State->RootDirectory));

    if (ParseObjectType->TypeInfo.ParseProcedure == NULL)
    {
        ObDereferenceObject(State->RootDirectory);
        *Status = STATUS_OBJECT_TYPE_MISMATCH;
        return FALSE;
    }

    for (;;)
    {
        RemainingName = *ObjectName;

        ObpCalloutStart(&CalloutIrql);
        *Status = ObpCallParseProcedure(ParseObjectType, State->RootDirectory,
                                        ObjectType, AccessState, AccessCheckMode,
                                        Attributes, ObjectName, &RemainingName,
                                        ParseContext, SecurityQos, &Object);
        ObpCalloutEnd(CalloutIrql, "Parse", ParseObjectType, Object);

        if (*Status != STATUS_REPARSE && *Status != STATUS_REPARSE_OBJECT)
        {
            if (!NT_SUCCESS(*Status)) Object = NULL;
            else if (Object == NULL) *Status = STATUS_OBJECT_NAME_NOT_FOUND;

            *FoundObject = Object;
            ObDereferenceObject(State->RootDirectory);
            return FALSE;
        }

        /* A reparse onto an absolute path starts again from the root */
        if (ObjectName->Length == 0 || ObjectName->Buffer == NULL ||
            ObjectName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
        {
            ObDereferenceObject(State->RootDirectory);
            State->RootDirectory = ObpRootDirectoryObject;
            State->RootHandle = NULL;
            return TRUE;
        }

        if (--MaxReparse == 0)
        {
            ObDereferenceObject(State->RootDirectory);
            if (Object == NULL)
            {
                *Status = (ParseObjectType == CmpKeyObjectType) ?
                          STATUS_INVALID_PARAMETER : STATUS_OBJECT_NAME_NOT_FOUND;
            }
            *FoundObject = Object;
            return FALSE;
        }
    }
}

NTSTATUS
NTAPI
ObpLookupObjectName(
    _In_opt_ HANDLE RootHandle,
    _Inout_ PUNICODE_STRING ObjectName,
    _In_ ULONG Attributes,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Inout_opt_ PVOID ParseContext,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _In_opt_ PVOID InsertObject,
    _Inout_ PACCESS_STATE AccessState,
    _Out_ POBP_LOOKUP_CONTEXT LookupContext,
    _Out_ PVOID *FoundObject)
{
    POBJECT_HEADER_NAME_INFO ObjectNameInfo;
    UNICODE_STRING ComponentName, RemainingName;
    KPROCESSOR_MODE AccessCheckMode;
    POBJECT_HEADER ObjectHeader;
    POBJECT_TYPE ParseObjectType = NULL;
    OB_PARSE_METHOD ParseRoutine = NULL;
    BOOLEAN RestartFromRoot;
    OBP_LOOKUP_STATE State;
    BOOLEAN FromRoot, Reparse, ParseAgain;
    KIRQL CalloutIrql;
    NTSTATUS Status;
    PWCHAR NewName;
    PVOID Object;

    PAGED_CODE();

    ObpInitializeLookupContext(LookupContext);

    RtlZeroMemory(&State, sizeof(State));
    State.RootHandle = RootHandle;
    State.MaxReparse = OBP_MAX_LOOKUP_REPARSE;

    Status = STATUS_SUCCESS;
    Object = NULL;
    *FoundObject = NULL;

    if (ObpCaseInsensitive &&
        (ObjectType == NULL || ObjectType->TypeInfo.CaseInsensitive))
    {
        Attributes |= OBJ_CASE_INSENSITIVE;
    }

    AccessCheckMode = (Attributes & OBJ_FORCE_ACCESS_CHECK) ? UserMode : AccessMode;

    FromRoot = FALSE;

    if (State.RootHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(State.RootHandle, 0, NULL, AccessMode,
                                           (PVOID *)&State.RootDirectory, NULL);
        if (!NT_SUCCESS(Status)) return Status;

        ObjectHeader = OBJECT_TO_OBJECT_HEADER(State.RootDirectory);

        /* An absolute path under a root handle is only legal for a file */
        if (ObjectName->Buffer != NULL &&
            ObjectName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR &&
            ObpGetObjectTypeFromHeader(ObjectHeader) != IoFileObjectType)
        {
            ObDereferenceObject(State.RootDirectory);
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        }

        if (ObpGetObjectTypeFromHeader(ObjectHeader) != ObpDirectoryObjectType)
        {
            if (!ObpParseRootHandleObject(&State, ObjectName, ObjectType,
                                          AccessState, AccessCheckMode, Attributes,
                                          ParseContext, SecurityQos,
                                          FoundObject, &Status))
            {
                return Status;
            }

            FromRoot = TRUE;
        }
        else if (ObjectName->Length == 0 || ObjectName->Buffer == NULL)
        {
            Status = ObReferenceObjectByPointer(State.RootDirectory, 0,
                                                ObjectType, AccessMode);
            if (NT_SUCCESS(Status)) *FoundObject = State.RootDirectory;

            ObDereferenceObject(State.RootDirectory);
            return Status;
        }
        else
        {
            State.LocalName = *ObjectName;
        }
    }
    else
    {
        State.RootDirectory = ObpRootDirectoryObject;

        if (ObjectName->Length == 0 || ObjectName->Buffer == NULL ||
            ObjectName->Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
        {
            return STATUS_OBJECT_PATH_SYNTAX_BAD;
        }

        /* The name is just "\" */
        if (ObjectName->Length == sizeof(OBJ_NAME_PATH_SEPARATOR))
        {
            if (State.RootDirectory != NULL)
            {
                Status = ObReferenceObjectByPointer(State.RootDirectory, 0,
                                                    ObjectType, AccessMode);
                if (NT_SUCCESS(Status)) *FoundObject = State.RootDirectory;
                return Status;
            }

            /* Only the very first directory may be inserted without a root */
            if (InsertObject == NULL)
            {
                ASSERT(FALSE);
                return STATUS_INVALID_PARAMETER;
            }

            Status = ObReferenceObjectByPointer(InsertObject, 0, ObjectType, AccessMode);
            if (NT_SUCCESS(Status)) *FoundObject = InsertObject;
            return Status;
        }

        FromRoot = TRUE;
    }

    Reparse = TRUE;

    while (TRUE)
    {
        if (FromRoot)
        {
            FromRoot = FALSE;
            if (!ObpPrepareParseFromRoot(&State, ObjectName, ObjectType,
                                         AccessMode, FoundObject, &Status))
            {
                return Status;
            }

            if (!State.SymLink)
            {
                Reparse = TRUE;
                State.MaxReparse = OBP_MAX_LOOKUP_REPARSE;
            }
        }

        if (!Reparse || State.MaxReparse == 0) break;

        RemainingName = State.LocalName;
        Reparse = FALSE;
        RestartFromRoot = FALSE;

        while (TRUE)
        {
            Object = NULL;

            if (RemainingName.Length != 0 &&
                RemainingName.Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
            {
                RemainingName.Buffer++;
                RemainingName.Length -= sizeof(OBJ_NAME_PATH_SEPARATOR);
            }

            ComponentName = RemainingName;
            while (RemainingName.Length != 0 &&
                   RemainingName.Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
            {
                RemainingName.Buffer++;
                RemainingName.Length -= sizeof(OBJ_NAME_PATH_SEPARATOR);
            }
            ComponentName.Length -= RemainingName.Length;

            if (ComponentName.Length == 0)
            {
                Status = STATUS_OBJECT_NAME_INVALID;
                break;
            }

            if (State.Directory == NULL) State.Directory = State.RootDirectory;

            /* Every directory on the way needs traverse access unless the
             * caller holds the privilege that waives the check */
            if (AccessCheckMode != KernelMode &&
                !(AccessState->Flags & TOKEN_HAS_TRAVERSE_PRIVILEGE))
            {
                ASSERT(State.ReferencedDirectory == NULL);
                ObReferenceObject(State.Directory);
                State.ReferencedDirectory = State.Directory;

                if (State.ParentDirectory != NULL &&
                    !ObpCheckTraverseAccess(State.ParentDirectory,
                                            DIRECTORY_TRAVERSE,
                                            AccessState,
                                            FALSE,
                                            AccessCheckMode,
                                            &Status))
                {
                    break;
                }
            }

            if (RemainingName.Length == 0)
            {
                if (State.ReferencedDirectory == NULL)
                {
                    ObReferenceObject(State.Directory);
                    State.ReferencedDirectory = State.Directory;
                }

                /* Hold the directory so the insert cannot race a creator */
                if (InsertObject != NULL)
                    ObpAcquireLookupContextLock(LookupContext, State.Directory);
            }

            Object = ObpLookupEntryDirectory(State.Directory,
                                             &ComponentName,
                                             Attributes,
                                             (InsertObject == NULL),
                                             LookupContext);

            if (Object == NULL)
            {
                if (RemainingName.Length != 0)
                {
                    Status = STATUS_OBJECT_PATH_NOT_FOUND;
                    break;
                }

                if (InsertObject == NULL)
                {
                    Status = STATUS_OBJECT_NAME_NOT_FOUND;
                    break;
                }

                if (!ObCheckCreateObjectAccess(State.Directory,
                                               (ObjectType == ObpDirectoryObjectType) ?
                                               DIRECTORY_CREATE_SUBDIRECTORY :
                                               DIRECTORY_CREATE_OBJECT,
                                               AccessState,
                                               &ComponentName,
                                               FALSE,
                                               AccessCheckMode,
                                               &Status))
                {
                    break;
                }

                ObjectHeader = OBJECT_TO_OBJECT_HEADER(InsertObject);

                /* A session directory only takes global names from a caller
                 * that is allowed to create them */
                if (State.RootDirectory->SessionId != -1 &&
                    (ObpGetObjectTypeFromHeader(ObjectHeader) == MmSectionObjectType ||
                     ObpGetObjectTypeFromHeader(ObjectHeader) == ObpSymbolicLinkObjectType) &&
                    State.RootDirectory->SessionId != PsGetCurrentProcessSessionId() &&
                    !SeSinglePrivilegeCheck(SeCreateGlobalPrivilege, AccessCheckMode) &&
                    !ObpIsUnsecureName(&ComponentName,
                                       BooleanFlagOn(Attributes, OBJ_CASE_INSENSITIVE)))
                {
                    Status = STATUS_ACCESS_DENIED;
                    break;
                }

                NewName = ExAllocatePoolWithTag(PagedPool, ComponentName.Length,
                                                OB_NAME_TAG);
                if (NewName == NULL ||
                    !ObpInsertEntryDirectory(State.Directory, LookupContext, ObjectHeader))
                {
                    if (NewName != NULL) ExFreePoolWithTag(NewName, OB_NAME_TAG);
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                ObReferenceObject(InsertObject);
                ObjectNameInfo = OBJECT_HEADER_TO_NAME_INFO(ObjectHeader);
                ObReferenceObject(State.Directory);

                RtlCopyMemory(NewName, ComponentName.Buffer, ComponentName.Length);

                if (ObjectNameInfo->Name.Buffer != NULL)
                    ExFreePoolWithTag(ObjectNameInfo->Name.Buffer, OB_NAME_TAG);

                ObjectNameInfo->Name.Buffer = NewName;
                ObjectNameInfo->Name.Length = ComponentName.Length;
                ObjectNameInfo->Name.MaximumLength = ComponentName.Length;

                Status = STATUS_SUCCESS;
                Object = InsertObject;
                break;
            }

            ParseAgain = TRUE;
            while (ParseAgain)
            {
                ParseAgain = FALSE;

                ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
                ParseObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);
                ParseRoutine = ParseObjectType->TypeInfo.ParseProcedure;

                /* An insert only follows a symbolic link, never a file system */
                if (ParseRoutine == NULL ||
                    (InsertObject != NULL &&
                     ParseRoutine != ObpParseSymbolicLink &&
                     !(ParseObjectType->TypeInfo.UseExtendedParameters &&
                       ParseObjectType->TypeInfo.ParseProcedureEx == ObpParseSymbolicLinkEx)))
                {
                    break;
                }

                State.Directory = NULL;
                InterlockedExchangeAddSizeT(&ObjectHeader->PointerCount, 1);
                ObpReleaseLookupContext(LookupContext);

                if (State.ReferencedDirectory != NULL)
                {
                    ObDereferenceObject(State.ReferencedDirectory);
                    State.ReferencedDirectory = NULL;
                }
                if (State.ReferencedParentDirectory != NULL)
                {
                    ObDereferenceObject(State.ReferencedParentDirectory);
                    State.ReferencedParentDirectory = NULL;
                }

                ObpCalloutStart(&CalloutIrql);
                Status = ObpCallParseProcedure(ParseObjectType, Object, ObjectType,
                                               AccessState, AccessCheckMode, Attributes,
                                               ObjectName, &RemainingName, ParseContext,
                                               SecurityQos, &Object);
                ObpCalloutEnd(CalloutIrql, "Parse", ParseObjectType, Object);

                ObDereferenceObject(&ObjectHeader->Body);

                if (Status != STATUS_REPARSE && Status != STATUS_REPARSE_OBJECT)
                {
                    if (!NT_SUCCESS(Status)) Object = NULL;
                    else if (Object == NULL) Status = STATUS_OBJECT_NAME_NOT_FOUND;
                    break;
                }

                Reparse = TRUE;
                if (--State.MaxReparse == 0)
                {
                    if (ParseObjectType == CmpKeyObjectType)
                        Status = STATUS_INVALID_PARAMETER;
                    Object = NULL;
                    break;
                }

                if (Status != STATUS_REPARSE_OBJECT &&
                    ObjectName->Buffer[0] != OBJ_NAME_PATH_SEPARATOR)
                {
                    /* A relative reparse under the root cannot go anywhere */
                    if (State.RootDirectory == ObpRootDirectoryObject)
                    {
                        Object = NULL;
                        Status = STATUS_OBJECT_NAME_NOT_FOUND;
                        Reparse = FALSE;
                    }
                    break;
                }

                if (State.RootHandle != NULL)
                {
                    ObDereferenceObject(State.RootDirectory);
                    State.RootHandle = NULL;
                }
                State.ParentDirectory = NULL;
                State.RootDirectory = ObpRootDirectoryObject;

                if (Status == STATUS_REPARSE_OBJECT)
                {
                    Reparse = FALSE;
                    if (Object == NULL)
                    {
                        Status = STATUS_OBJECT_NAME_NOT_FOUND;
                        break;
                    }

                    /* The parse gave us the next object outright */
                    ParseAgain = TRUE;
                    continue;
                }

                State.SymLink = TRUE;
                RestartFromRoot = TRUE;
                break;
            }

            if (RestartFromRoot) break;
            if (ParseRoutine != NULL &&
                (InsertObject == NULL ||
                 ParseRoutine == ObpParseSymbolicLink ||
                 (ParseObjectType->TypeInfo.UseExtendedParameters &&
                  ParseObjectType->TypeInfo.ParseProcedureEx == ObpParseSymbolicLinkEx)))
            {
                break;
            }

            if (RemainingName.Length == 0)
            {
                if (InsertObject == NULL)
                {
                    if (AccessCheckMode != KernelMode &&
                        !(AccessState->Flags & TOKEN_HAS_TRAVERSE_PRIVILEGE) &&
                        !ObpCheckTraverseAccess(State.Directory,
                                                DIRECTORY_TRAVERSE,
                                                AccessState,
                                                FALSE,
                                                AccessCheckMode,
                                                &Status))
                    {
                        Object = NULL;
                        break;
                    }

                    Status = ObReferenceObjectByPointer(Object, 0, ObjectType, AccessMode);
                    if (!NT_SUCCESS(Status)) Object = NULL;
                }

                break;
            }

            if (ObpGetObjectTypeFromHeader(ObjectHeader) != ObpDirectoryObjectType)
            {
                /* A trailing separator on a leaf is a path, not a type, error */
                if (ParseRoutine != NULL &&
                    !(RemainingName.Length == sizeof(WCHAR) &&
                      RemainingName.Buffer[0] == OBJ_NAME_PATH_SEPARATOR))
                {
                    Status = STATUS_OBJECT_PATH_NOT_FOUND;
                }
                else
                {
                    Status = STATUS_OBJECT_TYPE_MISMATCH;
                }

                Object = NULL;
                break;
            }

            if (State.ReferencedParentDirectory != NULL)
                ObDereferenceObject(State.ReferencedParentDirectory);

            State.ReferencedParentDirectory = State.ReferencedDirectory;
            State.ParentDirectory = State.Directory;
            State.Directory = Object;
            State.ReferencedDirectory = NULL;
        }

        if (RestartFromRoot)
        {
            FromRoot = TRUE;
            continue;
        }

        if (!Reparse) break;
    }

    if (!NT_SUCCESS(Status)) ObpReleaseLookupContext(LookupContext);

    if (State.DeviceMap != NULL) ObfDereferenceDeviceMap(State.DeviceMap);
    if (State.ReferencedDirectory != NULL) ObDereferenceObject(State.ReferencedDirectory);
    if (State.ReferencedParentDirectory != NULL)
        ObDereferenceObject(State.ReferencedParentDirectory);

    if (Object == NULL && (Status == STATUS_REPARSE || NT_SUCCESS(Status)))
        Status = STATUS_OBJECT_NAME_NOT_FOUND;

    if (State.RootHandle != NULL) ObDereferenceObject(State.RootDirectory);

    *FoundObject = Object;
    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
ObQueryNameString(
    _In_ PVOID Object,
    _Out_writes_bytes_opt_(Length) POBJECT_NAME_INFORMATION ObjectNameInfo,
    _In_ ULONG Length,
    _Out_ PULONG ReturnLength)
{
    POBJECT_HEADER ObjectHeader = OBJECT_TO_OBJECT_HEADER(Object);
    POBJECT_HEADER_NAME_INFO LocalInfo;
    POBJECT_DIRECTORY ParentDirectory;
    POBJECT_TYPE ObjectType;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN ObjectIsNamed;
    ULONG NameSize;
    PWCH ObjectName;

    LocalInfo = OBJECT_HEADER_TO_NAME_INFO(ObjectHeader);
    ObjectType = ObpGetObjectTypeFromHeader(ObjectHeader);

    /* A type that names its own objects answers for them */
    if (ObjectType->TypeInfo.QueryNameProcedure != NULL)
    {
        ObjectIsNamed = (LocalInfo != NULL && LocalInfo->Name.Length > 0);

        _SEH2_TRY
        {
            Status = ObjectType->TypeInfo.QueryNameProcedure(Object,
                                                             ObjectIsNamed,
                                                             ObjectNameInfo,
                                                             Length,
                                                             ReturnLength,
                                                             KernelMode);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    if (LocalInfo == NULL || LocalInfo->Name.Buffer == NULL)
    {
        _SEH2_TRY
        {
            *ReturnLength = sizeof(OBJECT_NAME_INFORMATION);

            if (*ReturnLength > Length)
                Status = STATUS_INFO_LENGTH_MISMATCH;
            else
                RtlInitEmptyUnicodeString(&ObjectNameInfo->Name, NULL, 0);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    _SEH2_TRY
    {
        /* Measure the whole path first, so a short buffer is reported before
         * anything is written into it */
        if (Object == ObpRootDirectoryObject)
        {
            NameSize = sizeof(OBJ_NAME_PATH_SEPARATOR);
        }
        else
        {
            ParentDirectory = LocalInfo->Directory;
            NameSize = sizeof(OBJ_NAME_PATH_SEPARATOR) + LocalInfo->Name.Length;

            while (ParentDirectory != ObpRootDirectoryObject && ParentDirectory != NULL)
            {
                LocalInfo = OBJECT_HEADER_TO_NAME_INFO(
                    OBJECT_TO_OBJECT_HEADER(ParentDirectory));

                if (LocalInfo != NULL && LocalInfo->Directory != NULL)
                {
                    NameSize += sizeof(OBJ_NAME_PATH_SEPARATOR) + LocalInfo->Name.Length;
                    ParentDirectory = LocalInfo->Directory;
                }
                else
                {
                    /* A directory with no name of its own stands in as "..." */
                    NameSize += sizeof(L"...") + sizeof(OBJ_NAME_PATH_SEPARATOR);
                    break;
                }
            }
        }

        *ReturnLength = NameSize + sizeof(OBJECT_NAME_INFORMATION) + sizeof(UNICODE_NULL);
        if (*ReturnLength > Length) _SEH2_YIELD(return STATUS_INFO_LENGTH_MISMATCH);

        LocalInfo = OBJECT_HEADER_TO_NAME_INFO(ObjectHeader);
        ObjectName = (PWCH)((ULONG_PTR)ObjectNameInfo + *ReturnLength);
        *--ObjectName = UNICODE_NULL;

        if (Object == ObpRootDirectoryObject)
        {
            *--ObjectName = OBJ_NAME_PATH_SEPARATOR;
        }
        else
        {
            /* The name is built backwards, from the object up to the root */
            ObjectName = (PWCH)((ULONG_PTR)ObjectName - LocalInfo->Name.Length);
            RtlCopyMemory(ObjectName, LocalInfo->Name.Buffer, LocalInfo->Name.Length);

            ParentDirectory = LocalInfo->Directory;
            while (ParentDirectory != ObpRootDirectoryObject && ParentDirectory != NULL)
            {
                LocalInfo = OBJECT_HEADER_TO_NAME_INFO(
                    OBJECT_TO_OBJECT_HEADER(ParentDirectory));

                *(--ObjectName) = OBJ_NAME_PATH_SEPARATOR;

                if (LocalInfo != NULL && LocalInfo->Name.Buffer != NULL)
                {
                    ObjectName = (PWCH)((ULONG_PTR)ObjectName - LocalInfo->Name.Length);
                    RtlCopyMemory(ObjectName, LocalInfo->Name.Buffer,
                                  LocalInfo->Name.Length);
                    ParentDirectory = LocalInfo->Directory;
                }
                else
                {
                    ObjectName = (PWCH)((ULONG_PTR)ObjectName -
                                        sizeof(L"...") + sizeof(UNICODE_NULL));
                    RtlCopyMemory(ObjectName, L"...", sizeof(L"..."));
                    break;
                }
            }

            *(--ObjectName) = OBJ_NAME_PATH_SEPARATOR;
        }

        ObjectNameInfo->Name.Length = (USHORT)NameSize;
        ObjectNameInfo->Name.MaximumLength = (USHORT)(NameSize + sizeof(UNICODE_NULL));
        ObjectNameInfo->Name.Buffer = ObjectName;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* EOF */
