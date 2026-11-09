/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Privileges and the checks made against them
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* Each privilege is the constant that names it, so none of these is a number */
const LUID SeCreateTokenPrivilege = {SE_CREATE_TOKEN_PRIVILEGE, 0};
const LUID SeAssignPrimaryTokenPrivilege = {SE_ASSIGNPRIMARYTOKEN_PRIVILEGE, 0};
const LUID SeLockMemoryPrivilege = {SE_LOCK_MEMORY_PRIVILEGE, 0};
const LUID SeIncreaseQuotaPrivilege = {SE_INCREASE_QUOTA_PRIVILEGE, 0};
const LUID SeUnsolicitedInputPrivilege = {SE_UNSOLICITED_INPUT_PRIVILEGE, 0};
const LUID SeTcbPrivilege = {SE_TCB_PRIVILEGE, 0};
const LUID SeSecurityPrivilege = {SE_SECURITY_PRIVILEGE, 0};
const LUID SeTakeOwnershipPrivilege = {SE_TAKE_OWNERSHIP_PRIVILEGE, 0};
const LUID SeLoadDriverPrivilege = {SE_LOAD_DRIVER_PRIVILEGE, 0};
const LUID SeSystemProfilePrivilege = {SE_SYSTEM_PROFILE_PRIVILEGE, 0};
const LUID SeSystemtimePrivilege = {SE_SYSTEMTIME_PRIVILEGE, 0};
const LUID SeProfileSingleProcessPrivilege = {SE_PROF_SINGLE_PROCESS_PRIVILEGE, 0};
const LUID SeIncreaseBasePriorityPrivilege = {SE_INC_BASE_PRIORITY_PRIVILEGE, 0};
const LUID SeCreatePagefilePrivilege = {SE_CREATE_PAGEFILE_PRIVILEGE, 0};
const LUID SeCreatePermanentPrivilege = {SE_CREATE_PERMANENT_PRIVILEGE, 0};
const LUID SeBackupPrivilege = {SE_BACKUP_PRIVILEGE, 0};
const LUID SeRestorePrivilege = {SE_RESTORE_PRIVILEGE, 0};
const LUID SeShutdownPrivilege = {SE_SHUTDOWN_PRIVILEGE, 0};
const LUID SeDebugPrivilege = {SE_DEBUG_PRIVILEGE, 0};
const LUID SeAuditPrivilege = {SE_AUDIT_PRIVILEGE, 0};
const LUID SeSystemEnvironmentPrivilege = {SE_SYSTEM_ENVIRONMENT_PRIVILEGE, 0};
const LUID SeChangeNotifyPrivilege = {SE_CHANGE_NOTIFY_PRIVILEGE, 0};
const LUID SeRemoteShutdownPrivilege = {SE_REMOTE_SHUTDOWN_PRIVILEGE, 0};
const LUID SeUndockPrivilege = {SE_UNDOCK_PRIVILEGE, 0};
const LUID SeSyncAgentPrivilege = {SE_SYNC_AGENT_PRIVILEGE, 0};
const LUID SeEnableDelegationPrivilege = {SE_ENABLE_DELEGATION_PRIVILEGE, 0};
const LUID SeManageVolumePrivilege = {SE_MANAGE_VOLUME_PRIVILEGE, 0};
const LUID SeImpersonatePrivilege = {SE_IMPERSONATE_PRIVILEGE, 0};
const LUID SeCreateGlobalPrivilege = {SE_CREATE_GLOBAL_PRIVILEGE, 0};
const LUID SeTrustedCredmanPrivilege = {SE_TRUSTED_CREDMAN_ACCESS_PRIVILEGE, 0};
const LUID SeRelabelPrivilege = {SE_RELABEL_PRIVILEGE, 0};
const LUID SeIncreaseWorkingSetPrivilege = {SE_INC_WORKING_SET_PRIVILEGE, 0};
const LUID SeTimeZonePrivilege = {SE_TIME_ZONE_PRIVILEGE, 0};
const LUID SeCreateSymbolicLinkPrivilege = {SE_CREATE_SYMBOLIC_LINK_PRIVILEGE, 0};
const LUID SeDelegateSessionUserImpersonatePrivilege =
    {SE_DELEGATE_SESSION_USER_IMPERSONATE_PRIVILEGE, 0};

/* PRIVATE FUNCTIONS **********************************************************/

CODE_SEG("INIT")
VOID
NTAPI
SepInitPrivileges(VOID)
{
    /* Every privilege is a constant of its own, so there is nothing to build */
}

/* Answers whether a token holds a privilege, and whether it is turned on */
static
BOOLEAN
SepTokenHoldsPrivilege(
    _In_ PTOKEN Token,
    _In_ const LUID *Privilege,
    _Out_ PBOOLEAN Enabled)
{
    ULONG Index;

    *Enabled = FALSE;

    if (Token == NULL) return FALSE;

    for (Index = 0; Index < Token->PrivilegeCount; Index++)
    {
        if (!RtlEqualLuid(&Token->Privileges[Index].Luid, Privilege)) continue;

        *Enabled = (Token->Privileges[Index].Attributes &
                    SE_PRIVILEGE_ENABLED) != 0;
        return TRUE;
    }

    return FALSE;
}

/*
 * Decides a set of privileges against a token and marks the ones that carried
 * the decision, so a caller can audit what it used. A set that must be held in
 * full fails on the first one missing; otherwise one is enough.
 */
BOOLEAN
NTAPI
SepPrivilegeCheck(
    _In_ PTOKEN Token,
    _In_ PLUID_AND_ATTRIBUTES Privileges,
    _In_ ULONG PrivilegeCount,
    _In_ ULONG PrivilegeControl,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Held = 0;
    ULONG Index;

    PAGED_CODE();

    for (Index = 0; Index < PrivilegeCount; Index++)
    {
        BOOLEAN Enabled = FALSE;
        BOOLEAN Counts;

        /* The kernel is trusted with every privilege there is */
        if (PreviousMode == KernelMode)
        {
            Counts = TRUE;
        }
        else
        {
            Counts = SepTokenHoldsPrivilege(Token,
                                            &Privileges[Index].Luid,
                                            &Enabled) && Enabled;
        }

        if (Counts)
        {
            Privileges[Index].Attributes |= SE_PRIVILEGE_USED_FOR_ACCESS;
            Held++;
        }
        else
        {
            Privileges[Index].Attributes &= ~SE_PRIVILEGE_USED_FOR_ACCESS;
        }
    }

    if (PrivilegeControl & PRIVILEGE_SET_ALL_NECESSARY)
        return (Held == PrivilegeCount);

    return (Held != 0);
}

/* PUBLIC FUNCTIONS ***********************************************************/

BOOLEAN
NTAPI
SePrivilegeCheck(
    _Inout_ PPRIVILEGE_SET RequiredPrivileges,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PAGED_CODE();

    /* The caller holds the context still, so what it reports is used as is */
    return SepPrivilegeCheck((PTOKEN)SeQuerySubjectContextToken(SubjectContext),
                             RequiredPrivileges->Privilege,
                             RequiredPrivileges->PrivilegeCount,
                             RequiredPrivileges->Control,
                             AccessMode);
}

BOOLEAN
NTAPI
SeSinglePrivilegeCheck(
    _In_ LUID PrivilegeValue,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    LUID_AND_ATTRIBUTES Privilege;
    BOOLEAN Result;

    PAGED_CODE();

    Privilege.Luid = PrivilegeValue;
    Privilege.Attributes = 0;

    SeCaptureSubjectContext(&SubjectContext);
    SeLockSubjectContext(&SubjectContext);

    Result = SepPrivilegeCheck((PTOKEN)SeQuerySubjectContextToken(&SubjectContext),
                               &Privilege,
                               1,
                               PRIVILEGE_SET_ALL_NECESSARY,
                               PreviousMode);

    SeUnlockSubjectContext(&SubjectContext);
    SeReleaseSubjectContext(&SubjectContext);

    return Result;
}

BOOLEAN
NTAPI
SeCheckAuditPrivilege(
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    LUID_AND_ATTRIBUTES Privilege;

    PAGED_CODE();

    Privilege.Luid = SeAuditPrivilege;
    Privilege.Attributes = 0;

    return SepPrivilegeCheck((PTOKEN)SeQuerySubjectContextToken(SubjectContext),
                             &Privilege,
                             1,
                             PRIVILEGE_SET_ALL_NECESSARY,
                             PreviousMode);
}

/* TODO: the use of the privilege against the object is not audited */
BOOLEAN
NTAPI
SeCheckPrivilegedObject(
    _In_ LUID PrivilegeValue,
    _In_ HANDLE ObjectHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ObjectHandle);
    UNREFERENCED_PARAMETER(DesiredAccess);

    return SeSinglePrivilegeCheck(PrivilegeValue, PreviousMode);
}

/*
 * Two kinds of access are not decided by a descriptor at all: reading or
 * writing a system ACL takes the security privilege, and taking ownership of
 * something takes the privilege for it. What is allowed that way moves out of
 * what is still wanted and into what has been granted.
 */
NTSTATUS
NTAPI
SePrivilegePolicyCheck(
    _Inout_ PACCESS_MASK DesiredAccess,
    _Inout_ PACCESS_MASK GrantedAccess,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ PTOKEN Token,
    _Out_opt_ PPRIVILEGE_SET *OutPrivilegeSet,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    LUID_AND_ATTRIBUTES Used[2];
    PPRIVILEGE_SET Set;
    PTOKEN Subject = Token;
    ULONG Count = 0;
    ULONG Size;

    PAGED_CODE();

    if (OutPrivilegeSet != NULL) *OutPrivilegeSet = NULL;

    if (Subject == NULL && SubjectContext != NULL)
        Subject = (PTOKEN)SeQuerySubjectContextToken(SubjectContext);

    if (*DesiredAccess & ACCESS_SYSTEM_SECURITY)
    {
        Used[Count].Luid = SeSecurityPrivilege;
        Used[Count].Attributes = 0;

        if (!SepPrivilegeCheck(Subject, &Used[Count], 1,
                               PRIVILEGE_SET_ALL_NECESSARY, PreviousMode))
        {
            return STATUS_PRIVILEGE_NOT_HELD;
        }

        *DesiredAccess &= ~ACCESS_SYSTEM_SECURITY;
        *GrantedAccess |= ACCESS_SYSTEM_SECURITY;
        Count++;
    }

    if (*DesiredAccess & WRITE_OWNER)
    {
        Used[Count].Luid = SeTakeOwnershipPrivilege;
        Used[Count].Attributes = 0;

        /* Not holding it is no refusal: the descriptor may allow it anyway */
        if (SepPrivilegeCheck(Subject, &Used[Count], 1,
                              PRIVILEGE_SET_ALL_NECESSARY, PreviousMode))
        {
            *DesiredAccess &= ~WRITE_OWNER;
            *GrantedAccess |= WRITE_OWNER;
            Count++;
        }
    }

    if (Count == 0 || OutPrivilegeSet == NULL) return STATUS_SUCCESS;

    Size = (ULONG)(FIELD_OFFSET(PRIVILEGE_SET, Privilege) +
                   (SIZE_T)Count * sizeof(LUID_AND_ATTRIBUTES));

    Set = ExAllocatePoolWithTag(PagedPool, Size, TAG_PRIVILEGE_SET);
    if (Set == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Set->PrivilegeCount = Count;
    Set->Control = 0;
    RtlCopyMemory(Set->Privilege, Used, Count * sizeof(LUID_AND_ATTRIBUTES));

    *OutPrivilegeSet = Set;

    return STATUS_SUCCESS;
}

/*
 * Records privileges a check used in the state that carried it. The state
 * holds a few itself and only moves to one of its own when it runs out, which
 * is what tells the delete whether there is anything to give back.
 */
NTSTATUS
NTAPI
SeAppendPrivileges(
    _Inout_ PACCESS_STATE AccessState,
    _In_ PPRIVILEGE_SET Privileges)
{
    PAUX_ACCESS_DATA AuxData = AccessState->AuxData;
    PPRIVILEGE_SET Existing;
    PPRIVILEGE_SET Bigger;
    ULONG Total;
    ULONG Size;

    PAGED_CODE();

    if (AuxData == NULL || AuxData->PrivilegesUsed == NULL)
        return STATUS_INVALID_PARAMETER;

    Existing = AuxData->PrivilegesUsed;
    Total = Existing->PrivilegeCount + Privileges->PrivilegeCount;

    if (Total <= INITIAL_PRIVILEGE_COUNT)
    {
        RtlCopyMemory(&Existing->Privilege[Existing->PrivilegeCount],
                      Privileges->Privilege,
                      Privileges->PrivilegeCount * sizeof(LUID_AND_ATTRIBUTES));
        Existing->PrivilegeCount = Total;

        return STATUS_SUCCESS;
    }

    Size = (ULONG)(FIELD_OFFSET(PRIVILEGE_SET, Privilege) +
                   (SIZE_T)Total * sizeof(LUID_AND_ATTRIBUTES));

    Bigger = ExAllocatePoolWithTag(PagedPool, Size, TAG_PRIVILEGE_SET);
    if (Bigger == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    Bigger->PrivilegeCount = Total;
    Bigger->Control = Existing->Control;

    RtlCopyMemory(Bigger->Privilege,
                  Existing->Privilege,
                  Existing->PrivilegeCount * sizeof(LUID_AND_ATTRIBUTES));
    RtlCopyMemory(&Bigger->Privilege[Existing->PrivilegeCount],
                  Privileges->Privilege,
                  Privileges->PrivilegeCount * sizeof(LUID_AND_ATTRIBUTES));

    /* Only a set the state already owned is ours to free */
    if (AccessState->PrivilegesAllocated)
        ExFreePoolWithTag(Existing, TAG_PRIVILEGE_SET);

    AuxData->PrivilegesUsed = Bigger;
    AccessState->PrivilegesAllocated = TRUE;

    return STATUS_SUCCESS;
}

VOID
NTAPI
SeFreePrivileges(
    _In_ PPRIVILEGE_SET Privileges)
{
    PAGED_CODE();

    if (Privileges != NULL) ExFreePoolWithTag(Privileges, TAG_PRIVILEGE_SET);
}

NTSTATUS
NTAPI
SeCaptureLuidAndAttributesArray(
    _In_ PLUID_AND_ATTRIBUTES Src,
    _In_ ULONG PrivilegeCount,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ PLUID_AND_ATTRIBUTES AllocatedMem,
    _In_ ULONG AllocatedLength,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PLUID_AND_ATTRIBUTES *Dest,
    _Inout_ PULONG Length)
{
    NTSTATUS Status = STATUS_SUCCESS;
    PLUID_AND_ATTRIBUTES Copy;
    SIZE_T Size;

    PAGED_CODE();

    *Dest = NULL;
    *Length = 0;

    if (PrivilegeCount == 0) return STATUS_SUCCESS;

    if (PreviousMode == KernelMode && !CaptureIfKernel)
    {
        *Dest = Src;
        return STATUS_SUCCESS;
    }

    if (PrivilegeCount > MAXULONG / sizeof(LUID_AND_ATTRIBUTES))
        return STATUS_INVALID_PARAMETER;

    Size = (SIZE_T)PrivilegeCount * sizeof(LUID_AND_ATTRIBUTES);

    if (AllocatedMem != NULL)
    {
        if (AllocatedLength < Size) return STATUS_BUFFER_TOO_SMALL;
        Copy = AllocatedMem;
    }
    else
    {
        Copy = ExAllocatePoolWithTag(PoolType, Size, TAG_PRIVILEGE_SET);
        if (Copy == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(Src, Size, TYPE_ALIGNMENT(LUID_AND_ATTRIBUTES));
        }

        RtlCopyMemory(Copy, Src, Size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        if (AllocatedMem == NULL) ExFreePoolWithTag(Copy, TAG_PRIVILEGE_SET);
        return Status;
    }

    *Dest = Copy;
    *Length = (ULONG)Size;

    return STATUS_SUCCESS;
}

VOID
NTAPI
SeReleaseLuidAndAttributesArray(
    _In_ PLUID_AND_ATTRIBUTES Privilege,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN CaptureIfKernel)
{
    PAGED_CODE();

    if (Privilege == NULL) return;
    if (PreviousMode == KernelMode && !CaptureIfKernel) return;

    ExFreePoolWithTag(Privilege, TAG_PRIVILEGE_SET);
}

/*
 * Answers, for a token a caller names, which of the privileges it asks about
 * that token holds. The set it passes is both the question and the answer.
 */
NTSTATUS
NTAPI
NtPrivilegeCheck(
    _In_ HANDLE ClientToken,
    _Inout_ PPRIVILEGE_SET RequiredPrivileges,
    _Out_ PBOOLEAN Result)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PLUID_AND_ATTRIBUTES Captured = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG CapturedLength = 0;
    BOOLEAN Holds = FALSE;
    ULONG Control = 0;
    ULONG Count = 0;
    PTOKEN Token;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(RequiredPrivileges,
                          FIELD_OFFSET(PRIVILEGE_SET, Privilege),
                          TYPE_ALIGNMENT(PRIVILEGE_SET));
            ProbeForWriteBoolean(Result);

            Count = RequiredPrivileges->PrivilegeCount;
            Control = RequiredPrivileges->Control;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status)) return Status;
    }
    else
    {
        Count = RequiredPrivileges->PrivilegeCount;
        Control = RequiredPrivileges->Control;
    }

    if (Count > MAXULONG / sizeof(LUID_AND_ATTRIBUTES))
        return STATUS_INVALID_PARAMETER;

    Status = ObReferenceObjectByHandle(ClientToken,
                                       TOKEN_QUERY,
                                       SeTokenObjectType,
                                       PreviousMode,
                                       (PVOID *)&Token,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /* Only an impersonation token answers for somebody else */
    if (Token->TokenType != TokenImpersonation)
    {
        ObDereferenceObject(Token);
        return STATUS_NO_IMPERSONATION_TOKEN;
    }

    if (Count != 0)
    {
        Status = SeCaptureLuidAndAttributesArray(RequiredPrivileges->Privilege,
                                                 Count,
                                                 PreviousMode,
                                                 NULL,
                                                 0,
                                                 PagedPool,
                                                 TRUE,
                                                 &Captured,
                                                 &CapturedLength);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(Token);
            return Status;
        }
    }

    SepAcquireTokenLockShared(Token);
    Holds = SepPrivilegeCheck(Token, Captured, Count, Control, PreviousMode);
    SepReleaseTokenLock(Token);

    /* The caller is told which privileges carried the answer */
    _SEH2_TRY
    {
        if (Count != 0)
        {
            RtlCopyMemory(RequiredPrivileges->Privilege,
                          Captured,
                          CapturedLength);
        }

        *Result = Holds;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (Captured != NULL)
        SeReleaseLuidAndAttributesArray(Captured, PreviousMode, TRUE);

    ObDereferenceObject(Token);

    return Status;
}

/* EOF */
