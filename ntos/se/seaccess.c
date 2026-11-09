/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Access state lifetime
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Sets up what one access check carries: the security the caller acts with,
 * the access still to be granted, and somewhere to record the privileges the
 * check uses. The auxiliary data belongs to the caller and has to outlive the
 * state, which only points at it.
 */
NTSTATUS
NTAPI
SeCreateAccessStateEx(
    _In_opt_ PETHREAD Thread,
    _In_ PEPROCESS Process,
    _Inout_ PACCESS_STATE AccessState,
    _In_ PAUX_ACCESS_DATA AuxData,
    _In_ ACCESS_MASK Access,
    _In_opt_ PGENERIC_MAPPING GenericMapping)
{
    ACCESS_MASK Remaining = Access;
    PTOKEN Token;

    PAGED_CODE();

    RtlZeroMemory(AccessState, sizeof(ACCESS_STATE));
    RtlZeroMemory(AuxData, sizeof(AUX_ACCESS_DATA));

    /*
     * The privileges a check uses start in the state's own set and move to a
     * bigger one only when more are needed than it holds, which is what
     * PrivilegesAllocated records.
     */
    AuxData->PrivilegesUsed = &AccessState->Privileges.PrivilegeSet;

    if (GenericMapping != NULL)
    {
        AuxData->GenericMapping = *GenericMapping;

        /* The type's own bits are what everything downstream compares */
        RtlMapGenericMask(&Remaining, GenericMapping);
    }

    AccessState->OriginalDesiredAccess = Access;
    AccessState->RemainingDesiredAccess = Remaining;
    AccessState->AuxData = AuxData;

    SeCaptureSubjectContextEx(Thread,
                              Process,
                              &AccessState->SubjectSecurityContext);

    /*
     * A token allowed to traverse any directory says so once here, so every
     * check made with this state can skip asking again.
     */
    /* TODO: TOKEN_IS_RESTRICTED is read back from these flags but nothing
     * seeds it, so a restricted token is checked as an unrestricted one */
    Token = (PTOKEN)SeQuerySubjectContextToken(&AccessState->SubjectSecurityContext);
    if (Token != NULL && (Token->TokenFlags & TOKEN_HAS_TRAVERSE_PRIVILEGE))
        AccessState->Flags |= TOKEN_HAS_TRAVERSE_PRIVILEGE;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SeCreateAccessState(
    _Inout_ PACCESS_STATE AccessState,
    _In_ PAUX_ACCESS_DATA AuxData,
    _In_ ACCESS_MASK Access,
    _In_opt_ PGENERIC_MAPPING GenericMapping)
{
    PAGED_CODE();

    return SeCreateAccessStateEx(PsGetCurrentThread(),
                                 PsGetCurrentProcess(),
                                 AccessState,
                                 AuxData,
                                 Access,
                                 GenericMapping);
}

VOID
NTAPI
SeSetAccessStateGenericMapping(
    _Inout_ PACCESS_STATE AccessState,
    _In_ PGENERIC_MAPPING GenericMapping)
{
    ((PAUX_ACCESS_DATA)AccessState->AuxData)->GenericMapping = *GenericMapping;
}

/*
 * Gives back everything the state took: a privilege set that outgrew the one
 * inside it, the names an audit attached to it, and the captured security.
 */
VOID
NTAPI
SeDeleteAccessState(
    _In_ PACCESS_STATE AccessState)
{
    PAUX_ACCESS_DATA AuxData = AccessState->AuxData;

    PAGED_CODE();

    if (AccessState->PrivilegesAllocated &&
        AuxData != NULL &&
        AuxData->PrivilegesUsed != NULL)
    {
        ExFreePoolWithTag(AuxData->PrivilegesUsed, TAG_PRIVILEGE_SET);
        AuxData->PrivilegesUsed = NULL;
        AccessState->PrivilegesAllocated = FALSE;
    }

    if (AccessState->ObjectName.Buffer != NULL)
    {
        ExFreePool(AccessState->ObjectName.Buffer);
        RtlInitEmptyUnicodeString(&AccessState->ObjectName, NULL, 0);
    }

    if (AccessState->ObjectTypeName.Buffer != NULL)
    {
        ExFreePool(AccessState->ObjectTypeName.Buffer);
        RtlInitEmptyUnicodeString(&AccessState->ObjectTypeName, NULL, 0);
    }

    SeReleaseSubjectContext(&AccessState->SubjectSecurityContext);
}

/* EOF */
