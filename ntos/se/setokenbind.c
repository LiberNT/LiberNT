/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Giving a token to a process or a thread, and telling two apart
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Whether a token speaks for whoever an object says owns it, which is what
 * lets an owner change what its own object allows.
 */
BOOLEAN
NTAPI
SepTokenIsOwner(
    _In_ PACCESS_TOKEN _Token,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ BOOLEAN TokenLocked)
{
    PTOKEN Token = _Token;
    BOOLEAN Defaulted;
    BOOLEAN Owns;
    PSID Owner = NULL;

    PAGED_CODE();

    if (!NT_SUCCESS(RtlGetOwnerSecurityDescriptor(SecurityDescriptor,
                                                  &Owner,
                                                  &Defaulted)) ||
        Owner == NULL)
    {
        return FALSE;
    }

    if (!TokenLocked) SepAcquireTokenLockShared(Token);

    Owns = SepSidInToken(Token, Owner);

    if (!TokenLocked) SepReleaseTokenLock(Token);

    return Owns;
}

/*
 * Whether a process may wear a token at the level it asks for. A process can
 * always wear what it already is, and a token given at a level may not be
 * worn as if it had been given at a higher one.
 */
BOOLEAN
NTAPI
SeTokenCanImpersonate(
    _In_ PTOKEN ProcessToken,
    _In_ PTOKEN TokenToImpersonate,
    _In_ SECURITY_IMPERSONATION_LEVEL ImpersonationLevel)
{
    BOOLEAN Allowed = TRUE;
    ULONG i;
    ULONG j;

    PAGED_CODE();

    /* Nothing is kept from anybody below the level where a token is acted on */
    if (ImpersonationLevel < SecurityImpersonation) return TRUE;

    SepAcquireTokenLockShared(ProcessToken);
    SepAcquireTokenLockShared(TokenToImpersonate);

    if (!RtlEqualLuid(&ProcessToken->AuthenticationId,
                      &TokenToImpersonate->AuthenticationId))
    {
        /* A token of another logon may be worn only by one that is trusted */
        if (!(ProcessToken->TokenFlags & TOKEN_HAS_IMPERSONATE_PRIVILEGE))
            Allowed = FALSE;
    }

    /* What was cut down cannot be worn by what was not cut down the same way */
    if (Allowed && TokenToImpersonate->RestrictedSidCount != 0)
    {
        for (i = 0; i < TokenToImpersonate->RestrictedSidCount; i++)
        {
            for (j = 0; j < ProcessToken->RestrictedSidCount; j++)
            {
                if (RtlEqualSid(TokenToImpersonate->RestrictedSids[i].Sid,
                                ProcessToken->RestrictedSids[j].Sid))
                {
                    break;
                }
            }

            if (j == ProcessToken->RestrictedSidCount)
            {
                Allowed = FALSE;
                break;
            }
        }
    }

    SepReleaseTokenLock(TokenToImpersonate);
    SepReleaseTokenLock(ProcessToken);

    return Allowed;
}

/* A token made from another one says which it came from */
NTSTATUS
NTAPI
SeIsTokenChild(
    _In_ PTOKEN Token,
    _Out_ PBOOLEAN IsChild)
{
    PTOKEN Primary;
    LUID Mine;

    PAGED_CODE();

    *IsChild = FALSE;

    Primary = PsReferencePrimaryToken(PsGetCurrentProcess());
    if (Primary == NULL) return STATUS_NO_TOKEN;

    SepAcquireTokenLockShared(Primary);
    Mine = Primary->TokenId;
    SepReleaseTokenLock(Primary);

    PsDereferencePrimaryToken(Primary);

    SepAcquireTokenLockShared(Token);
    *IsChild = RtlEqualLuid(&Token->ParentTokenId, &Mine);
    SepReleaseTokenLock(Token);

    return STATUS_SUCCESS;
}

/* Two tokens made from the same one are of the same standing */
NTSTATUS
NTAPI
SeIsTokenSibling(
    _In_ PTOKEN Token,
    _Out_ PBOOLEAN IsSibling)
{
    PTOKEN Primary;
    LUID Parent;
    LUID Logon;

    PAGED_CODE();

    *IsSibling = FALSE;

    Primary = PsReferencePrimaryToken(PsGetCurrentProcess());
    if (Primary == NULL) return STATUS_NO_TOKEN;

    SepAcquireTokenLockShared(Primary);
    Parent = Primary->ParentTokenId;
    Logon = Primary->AuthenticationId;
    SepReleaseTokenLock(Primary);

    PsDereferencePrimaryToken(Primary);

    SepAcquireTokenLockShared(Token);
    *IsSibling = (BOOLEAN)(RtlEqualLuid(&Token->ParentTokenId, &Parent) &&
                           RtlEqualLuid(&Token->AuthenticationId, &Logon));
    SepReleaseTokenLock(Token);

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
SeIsParentOfChildAppContainer(
    _In_ PSID ParentPackageSid,
    _In_ PSID ChildPackageSid)
{
    UNREFERENCED_PARAMETER(ParentPackageSid);
    UNREFERENCED_PARAMETER(ChildPackageSid);

    /* TODO: packages are not arranged in families, so none is another's parent */
    return FALSE;
}

/* The label an object is given, which says who may write to it */
NTSTATUS
NTAPI
SeSetObjectMandatoryLabel(
    _In_ PVOID Object,
    _In_ ULONG Rid,
    _In_ ULONG Policy)
{
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(Rid);
    UNREFERENCED_PARAMETER(Policy);

    /* TODO: an object's label is settled where the object is made, not here */
    return STATUS_NOT_IMPLEMENTED;
}

/*
 * Gives a process the token it is to run with. The process is not running
 * yet, so nothing can be reading the token as it is handed over.
 */
VOID
NTAPI
SeAssignPrimaryToken(
    _In_ PEPROCESS Process,
    _In_ PTOKEN Token)
{
    PAGED_CODE();

    if (Token == NULL) return;

    ObReferenceObject(Token);

    SepAcquireTokenLockExclusive(Token);
    Token->TokenInUse = TRUE;
    SepReleaseTokenLock(Token);

    ObInitializeFastReference(&Process->Token, Token);
}

VOID
NTAPI
SeDeassignPrimaryToken(
    _Inout_ PEPROCESS Process)
{
    PTOKEN Token;

    PAGED_CODE();

    Token = ObFastReferenceObject(&Process->Token);
    if (Token == NULL) return;

    ObFastDereferenceObject(&Process->Token, Token);
    ObDereferenceObject(Token);
}

/*
 * Puts another token in a running process's place. Only a token that stands
 * on its own and is not already somebody's may be handed over.
 */
NTSTATUS
NTAPI
SeExchangePrimaryToken(
    _In_ PEPROCESS Process,
    _In_ PACCESS_TOKEN NewAccessToken,
    _Out_ PACCESS_TOKEN *OldAccessToken)
{
    PTOKEN New = NewAccessToken;
    PTOKEN Old;

    PAGED_CODE();

    *OldAccessToken = NULL;

    if (New->TokenType != TokenPrimary) return STATUS_BAD_TOKEN_TYPE;

    SepAcquireTokenLockExclusive(New);

    if (New->TokenInUse)
    {
        SepReleaseTokenLock(New);
        return STATUS_TOKEN_ALREADY_IN_USE;
    }

    New->TokenInUse = TRUE;
    SepReleaseTokenLock(New);

    ObReferenceObject(New);

    Old = ObFastReplaceObject(&Process->Token, New);

    if (Old != NULL)
    {
        SepAcquireTokenLockExclusive(Old);
        Old->TokenInUse = FALSE;
        SepReleaseTokenLock(Old);
    }

    *OldAccessToken = Old;
    return STATUS_SUCCESS;
}

/* SYSTEM CALLS ***************************************************************/

/*
 * Opens the token a thread is wearing. A token that was only lent to the
 * thread is copied, because what the caller holds must outlive the lending.
 */
static
NTSTATUS
SepOpenThreadToken(
    _In_ HANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN OpenAsSelf,
    _In_ ULONG HandleAttributes,
    _Out_ PHANDLE TokenHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    SECURITY_IMPERSONATION_LEVEL Level;
    SE_IMPERSONATION_STATE Lending;
    BOOLEAN CopyOnOpen;
    BOOLEAN EffectiveOnly;
    PACCESS_TOKEN Worn;
    PTOKEN Opened = NULL;
    PETHREAD Thread;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(TokenHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_QUERY_INFORMATION,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID *)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Worn = PsReferenceImpersonationToken(Thread,
                                         &CopyOnOpen,
                                         &EffectiveOnly,
                                         &Level);
    if (Worn == NULL)
    {
        ObDereferenceObject(Thread);
        return STATUS_NO_TOKEN;
    }

    if (CopyOnOpen)
    {
        /* The thread was lent this token, so the caller is given a copy and
         * the lending ends here */
        Status = SeCopyClientToken(Worn, Level, PreviousMode, (PACCESS_TOKEN *)&Opened);

        if (NT_SUCCESS(Status)) PsDisableImpersonation(Thread, &Lending);
    }
    else
    {
        Opened = Worn;
        ObReferenceObject(Opened);
    }

    PsDereferenceImpersonationToken(Worn);
    ObDereferenceObject(Thread);

    if (!NT_SUCCESS(Status)) return Status;

    /* Whoever opens on its own behalf is judged as itself, not as what it
     * is wearing */
    Status = ObOpenObjectByPointer(Opened,
                                   HandleAttributes |
                                       (OpenAsSelf ? OBJ_FORCE_ACCESS_CHECK : 0),
                                   NULL,
                                   DesiredAccess,
                                   SeTokenObjectType,
                                   PreviousMode,
                                   &Handle);

    ObDereferenceObject(Opened);

    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        *TokenHandle = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) ObCloseHandle(Handle, PreviousMode);

    return Status;
}

NTSTATUS
NTAPI
NtOpenThreadTokenEx(
    _In_ HANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN OpenAsSelf,
    _In_ ULONG HandleAttributes,
    _Out_ PHANDLE TokenHandle)
{
    return SepOpenThreadToken(ThreadHandle,
                              DesiredAccess,
                              OpenAsSelf,
                              HandleAttributes,
                              TokenHandle);
}

NTSTATUS
NTAPI
NtOpenThreadToken(
    _In_ HANDLE ThreadHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN OpenAsSelf,
    _Out_ PHANDLE TokenHandle)
{
    return SepOpenThreadToken(ThreadHandle,
                              DesiredAccess,
                              OpenAsSelf,
                              0,
                              TokenHandle);
}

/* Wearing the token of somebody who never said who they are */
NTSTATUS
NTAPI
NtImpersonateAnonymousToken(
    _In_ HANDLE ThreadHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PTOKEN Anonymous;
    PETHREAD Thread;
    NTSTATUS Status;
    ULONG Allowed = 0;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ThreadHandle,
                                       THREAD_IMPERSONATE,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID *)&Thread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * Whether an unknown caller counts as part of the world is the system's
     * to settle, and it is asked once here.
     */
    Status = SepRegQueryHelper(L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Lsa",
                               L"EveryoneIncludesAnonymous",
                               REG_DWORD,
                               sizeof(Allowed),
                               &Allowed);

    Anonymous = NT_SUCCESS(Status) && Allowed != 0
                    ? SeAnonymousLogonToken
                    : SeAnonymousLogonTokenNoEveryone;

    Status = PsImpersonateClient(Thread,
                                 Anonymous,
                                 FALSE,
                                 FALSE,
                                 SecurityImpersonation);

    ObDereferenceObject(Thread);
    return Status;
}

/*
 * Whether two tokens say the same thing. Two tokens made from the same one
 * at the same moment are the same; anything either was told since is not.
 */
NTSTATUS
NTAPI
NtCompareTokens(
    _In_ HANDLE FirstTokenHandle,
    _In_ HANDLE SecondTokenHandle,
    _Out_ PBOOLEAN Equal)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PTOKEN First = NULL;
    PTOKEN Second = NULL;
    BOOLEAN Same = FALSE;
    NTSTATUS Status;
    ULONG i;
    ULONG j;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteBoolean(Equal);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(FirstTokenHandle,
                                       TOKEN_QUERY,
                                       SeTokenObjectType,
                                       PreviousMode,
                                       (PVOID *)&First,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(SecondTokenHandle,
                                           TOKEN_QUERY,
                                           SeTokenObjectType,
                                           PreviousMode,
                                           (PVOID *)&Second,
                                           NULL);
        if (!NT_SUCCESS(Status)) ObDereferenceObject(First);
    }

    if (!NT_SUCCESS(Status)) return Status;

    if (First == Second)
    {
        Same = TRUE;
    }
    else
    {
        SepAcquireTokenLockShared(First);
        SepAcquireTokenLockShared(Second);

        Same = (BOOLEAN)(First->UserAndGroupCount == Second->UserAndGroupCount &&
                         First->RestrictedSidCount == Second->RestrictedSidCount &&
                         First->PrivilegeCount == Second->PrivilegeCount &&
                         RtlEqualLuid(&First->AuthenticationId,
                                      &Second->AuthenticationId) &&
                         First->TokenType == Second->TokenType);

        for (i = 0; Same && i < First->UserAndGroupCount; i++)
        {
            for (j = 0; j < Second->UserAndGroupCount; j++)
            {
                if (First->UserAndGroups[i].Attributes !=
                    Second->UserAndGroups[j].Attributes)
                {
                    continue;
                }

                if (RtlEqualSid(First->UserAndGroups[i].Sid,
                                Second->UserAndGroups[j].Sid))
                {
                    break;
                }
            }

            if (j == Second->UserAndGroupCount) Same = FALSE;
        }

        for (i = 0; Same && i < First->PrivilegeCount; i++)
        {
            for (j = 0; j < Second->PrivilegeCount; j++)
            {
                if (RtlEqualLuid(&First->Privileges[i].Luid,
                                 &Second->Privileges[j].Luid))
                {
                    break;
                }
            }

            if (j == Second->PrivilegeCount) Same = FALSE;
        }

        SepReleaseTokenLock(Second);
        SepReleaseTokenLock(First);
    }

    ObDereferenceObject(Second);
    ObDereferenceObject(First);

    _SEH2_TRY
    {
        *Equal = Same;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}
