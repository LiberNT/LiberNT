/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The token a process runs with, and the token a thread wears
 *              while it acts for somebody else
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include "pspriv.h"
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PTOKEN PspBootAccessToken;

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * The block a thread keeps while it wears somebody else's token. It is
 * allocated the first time a thread impersonates and kept afterwards, so a
 * thread that impersonates repeatedly allocates once.
 */
static
NTSTATUS
PspEnsureImpersonationInfo(
    _In_ PETHREAD Thread)
{
    PPS_IMPERSONATION_INFORMATION Info;

    PAGED_CODE();

    if (Thread->ImpersonationInfo != NULL) return STATUS_SUCCESS;

    Info = ExAllocatePoolWithTag(NonPagedPool,
                                 sizeof(PS_IMPERSONATION_INFORMATION),
                                 TAG_PS_IMPERSONATION);
    if (Info == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Info, sizeof(*Info));
    Thread->ImpersonationInfo = Info;

    return STATUS_SUCCESS;
}

/*
 * Tells the thread's own block in user memory whether it is impersonating,
 * which is what a caller in user mode reads to find out without a call.
 */
VOID
NTAPI
PspWriteTebImpersonationInfo(
    _In_ PETHREAD Thread,
    _In_ PEPROCESS Process)
{
    PTEB Teb = Thread->Tcb.Teb;
    KAPC_STATE ApcState;
    BOOLEAN Active;

    PAGED_CODE();

    if (Teb == NULL) return;

    Active = (BOOLEAN)(Thread->ActiveImpersonationInfo != 0);

    KeStackAttachProcess(&Process->Pcb, &ApcState);

    _SEH2_TRY
    {
        Teb->IsImpersonating = Active ? 1 : 0;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* The block is the thread's own memory; a thread that unmapped it
         * gets no notice, which is all a failure here means */
    }
    _SEH2_END;

    KeUnstackDetachProcess(&ApcState);
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Gives a new process the token it will run with. A child is given a copy of
 * its parent's token cut down to the child's own session; a process with no
 * parent is given the token the system booted with.
 */
NTSTATUS
NTAPI
PspInitializeProcessSecurity(
    _In_ PEPROCESS Process,
    _In_opt_ PEPROCESS Parent)
{
    PTOKEN Token = NULL;
    PTOKEN Parented;
    NTSTATUS Status;

    PAGED_CODE();

    if (Parent == NULL)
    {
        /*
         * The first process of all runs with the token the security manager
         * built while booting. Until that token exists there is nothing to
         * assign, and the process runs untokened for the moment.
         */
        if (PspBootAccessToken == NULL) return STATUS_SUCCESS;

        ObInitializeFastReference(&Process->Token, NULL);
        SeAssignPrimaryToken(Process, PspBootAccessToken);
        return STATUS_SUCCESS;
    }

    Parented = PsReferencePrimaryToken(Parent);
    if (Parented == NULL) return STATUS_NO_TOKEN;

    Status = SeSubProcessToken(Parented,
                               &Token,
                               TRUE,
                               MmGetSessionId(Process));
    PsDereferencePrimaryToken(Parented);

    if (!NT_SUCCESS(Status)) return Status;

    ObInitializeFastReference(&Process->Token, NULL);
    SeAssignPrimaryToken(Process, Token);

    /* The reference the copy was made with is given back: the process has
     * taken its own */
    ObDereferenceObject(Token);

    return STATUS_SUCCESS;
}

VOID
NTAPI
PspDeleteProcessSecurity(
    _In_ PEPROCESS Process)
{
    PAGED_CODE();

    SeDeassignPrimaryToken(Process);
}

VOID
NTAPI
PspDeleteThreadSecurity(
    _In_ PETHREAD Thread)
{
    PPS_IMPERSONATION_INFORMATION Info = Thread->ImpersonationInfo;

    PAGED_CODE();

    if (Info == NULL) return;

    if (Thread->ActiveImpersonationInfo && Info->Token != NULL)
        ObDereferenceObject(Info->Token);

    PspClearCrossThreadFlag(Thread, CT_ACTIVE_IMPERSONATION_INFO_BIT);
    Thread->ImpersonationInfo = NULL;

    ExFreePoolWithTag(Info, TAG_PS_IMPERSONATION);
}

/*
 * Puts another token in a running process's place. A caller that names the
 * token by handle needs the privilege to assign one; a caller inside the
 * kernel that already holds the token does not go through a handle at all.
 */
NTSTATUS
NTAPI
PspSetPrimaryToken(
    _In_ PEPROCESS Process,
    _In_opt_ HANDLE TokenHandle,
    _In_opt_ PACCESS_TOKEN Token,
    _In_ BOOLEAN Duplicate)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PACCESS_TOKEN NewToken = Token;
    PACCESS_TOKEN OldToken = NULL;
    BOOLEAN Referenced = FALSE;
    BOOLEAN IsChild = FALSE;
    BOOLEAN IsSibling = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    if (TokenHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(TokenHandle,
                                           TOKEN_ASSIGN_PRIMARY,
                                           SeTokenObjectType,
                                           PreviousMode,
                                           &NewToken,
                                           NULL);
        if (!NT_SUCCESS(Status)) return Status;

        Referenced = TRUE;
    }

    if (NewToken == NULL) return STATUS_INVALID_PARAMETER;

    if (Duplicate)
    {
        PACCESS_TOKEN Copy;

        Status = SeCopyClientToken(NewToken,
                                   SecurityImpersonation,
                                   KernelMode,
                                   &Copy);
        if (Referenced) ObDereferenceObject(NewToken);
        if (!NT_SUCCESS(Status)) return Status;

        NewToken = Copy;
        Referenced = TRUE;
    }

    /*
     * A token may be given to a process only when it stands for the same
     * logon the process already belongs to, which is what being a child or a
     * sibling of the process's own token means.
     */
    Status = SeIsTokenChild(NewToken, &IsChild);
    if (NT_SUCCESS(Status) && !IsChild)
        Status = SeIsTokenSibling(NewToken, &IsSibling);

    if (NT_SUCCESS(Status) && !IsChild && !IsSibling)
    {
        if (!SeSinglePrivilegeCheck(SeAssignPrimaryTokenPrivilege,
                                    PreviousMode))
        {
            Status = STATUS_PRIVILEGE_NOT_HELD;
        }
    }

    if (NT_SUCCESS(Status))
        Status = SeExchangePrimaryToken(Process, NewToken, &OldToken);

    if (Referenced) ObDereferenceObject(NewToken);

    if (NT_SUCCESS(Status))
    {
        Process->PrimaryTokenFrozen = TRUE;
        if (OldToken != NULL) ObDereferenceObject(OldToken);
    }

    return Status;
}

NTSTATUS
NTAPI
PspAssignPrimaryToken(
    _In_ PEPROCESS Process,
    _In_opt_ HANDLE TokenHandle,
    _In_opt_ PACCESS_TOKEN Token)
{
    PAGED_CODE();

    return PspSetPrimaryToken(Process, TokenHandle, Token, FALSE);
}

/*
 * Hands back the token the process runs with, with a reference on it that
 * the caller gives back through PsDereferencePrimaryToken.
 */
/*
 * The declaration in the published headers says this hands back a handle.
 * It does not: it hands back the token itself, with a reference on it.
 */
HANDLE
NTAPI
PsReferencePrimaryToken(
    _Inout_ PEPROCESS Process)
{
    PACCESS_TOKEN Token;

    PAGED_CODE();

    Token = ObFastReferenceObject(&Process->Token);
    if (Token != NULL) return Token;

    /*
     * The quick path had no reference left to lend, so the slow one takes a
     * proper reference under the process's own lock.
     */
    PspLockProcessSecurityShared(Process);
    Token = ObFastReferenceObjectLocked(&Process->Token);
    PspUnlockProcessSecurityShared(Process);

    return Token;
}

VOID
NTAPI
PsDereferencePrimaryToken(
    _In_ PACCESS_TOKEN Token)
{
    PAGED_CODE();

    if (Token != NULL) ObDereferenceObject(Token);
}

/*
 * Hands back the token the thread is wearing, if it is wearing one, together
 * with how it was lent: a token lent only for a check may not be opened, and
 * one lent at a level below impersonation may not be acted on.
 */
PACCESS_TOKEN
NTAPI
PsReferenceImpersonationToken(
    _Inout_ PETHREAD Thread,
    _Out_ PBOOLEAN CopyOnOpen,
    _Out_ PBOOLEAN EffectiveOnly,
    _Out_ PSECURITY_IMPERSONATION_LEVEL ImpersonationLevel)
{
    PACCESS_TOKEN Token = NULL;

    PAGED_CODE();

    if (!Thread->ActiveImpersonationInfo) return NULL;

    PspLockThreadSecurityShared(Thread);

    if (Thread->ActiveImpersonationInfo && Thread->ImpersonationInfo != NULL)
    {
        Token = Thread->ImpersonationInfo->Token;
        *CopyOnOpen = Thread->ImpersonationInfo->CopyOnOpen;
        *EffectiveOnly = Thread->ImpersonationInfo->EffectiveOnly;
        *ImpersonationLevel = Thread->ImpersonationInfo->ImpersonationLevel;

        if (Token != NULL) ObReferenceObject(Token);
    }

    PspUnlockThreadSecurityShared(Thread);

    return Token;
}

/*
 * The token that decides what the thread may do: the one it wears if it is
 * wearing one, otherwise the one its process runs with.
 */
PACCESS_TOKEN
NTAPI
PsReferenceEffectiveToken(
    _In_ PETHREAD Thread,
    _Out_ PTOKEN_TYPE TokenType,
    _Out_ PBOOLEAN EffectiveOnly,
    _Out_ PSECURITY_IMPERSONATION_LEVEL ImpersonationLevel)
{
    PEPROCESS Process = THREAD_TO_PROCESS(Thread);
    PACCESS_TOKEN Token = NULL;

    PAGED_CODE();

    if (!Thread->ActiveImpersonationInfo)
    {
        *TokenType = TokenPrimary;
        *EffectiveOnly = FALSE;
        Token = PsReferencePrimaryToken(Process);
        return Token;
    }

    PspLockThreadSecurityShared(Thread);

    if (Thread->ActiveImpersonationInfo && Thread->ImpersonationInfo != NULL)
    {
        *TokenType = TokenImpersonation;
        *EffectiveOnly = Thread->ImpersonationInfo->EffectiveOnly;
        *ImpersonationLevel = Thread->ImpersonationInfo->ImpersonationLevel;
        Token = Thread->ImpersonationInfo->Token;
        if (Token != NULL) ObReferenceObject(Token);
    }

    PspUnlockThreadSecurityShared(Thread);

    if (Token == NULL)
    {
        *TokenType = TokenPrimary;
        *EffectiveOnly = FALSE;
        Token = PsReferencePrimaryToken(Process);
    }

    return Token;
}

/*
 * Makes a thread wear a token. A token the thread is not allowed to wear at
 * the level asked for is refused, and passing no token takes off whatever
 * the thread is wearing.
 */
NTSTATUS
NTAPI
PsImpersonateClient(
    _Inout_ PETHREAD Thread,
    _In_opt_ PACCESS_TOKEN Token,
    _In_ BOOLEAN CopyOnOpen,
    _In_ BOOLEAN EffectiveOnly,
    _In_ SECURITY_IMPERSONATION_LEVEL ImpersonationLevel)
{
    PEPROCESS Process = THREAD_TO_PROCESS(Thread);
    PACCESS_TOKEN OldToken = NULL;
    PACCESS_TOKEN Primary;
    NTSTATUS Status;

    PAGED_CODE();

    if (Token == NULL)
    {
        PsRevertThreadToSelf(Thread);
        return STATUS_SUCCESS;
    }

    /*
     * Wearing a token at the level where it can be acted on is only allowed
     * to a process that could have obtained the token itself.
     */
    if (ImpersonationLevel >= SecurityImpersonation)
    {
        Primary = PsReferencePrimaryToken(Process);
        if (Primary == NULL) return STATUS_NO_TOKEN;

        if (!SeTokenCanImpersonate(Primary, Token, ImpersonationLevel))
        {
            PsDereferencePrimaryToken(Primary);
            return STATUS_ACCESS_DENIED;
        }

        PsDereferencePrimaryToken(Primary);
    }

    PspLockThreadSecurityExclusive(Thread);

    Status = PspEnsureImpersonationInfo(Thread);
    if (!NT_SUCCESS(Status))
    {
        PspUnlockThreadSecurityExclusive(Thread);
        return Status;
    }

    if (Thread->ActiveImpersonationInfo)
        OldToken = Thread->ImpersonationInfo->Token;

    ObReferenceObject(Token);
    Thread->ImpersonationInfo->Token = Token;
    Thread->ImpersonationInfo->CopyOnOpen = CopyOnOpen;
    Thread->ImpersonationInfo->EffectiveOnly = EffectiveOnly;
    Thread->ImpersonationInfo->ImpersonationLevel = ImpersonationLevel;
    PspSetCrossThreadFlag(Thread, CT_ACTIVE_IMPERSONATION_INFO_BIT);

    PspUnlockThreadSecurityExclusive(Thread);

    if (OldToken != NULL) ObDereferenceObject(OldToken);

    PspWriteTebImpersonationInfo(Thread, Process);

    return STATUS_SUCCESS;
}

/*
 * Takes off whatever the thread is wearing, so that from here it acts as the
 * process it belongs to again.
 */
VOID
NTAPI
PsRevertThreadToSelf(
    _Inout_ PETHREAD Thread)
{
    PACCESS_TOKEN Token = NULL;

    PAGED_CODE();

    if (!Thread->ActiveImpersonationInfo) return;

    PspLockThreadSecurityExclusive(Thread);

    if (Thread->ActiveImpersonationInfo)
    {
        Token = Thread->ImpersonationInfo->Token;
        Thread->ImpersonationInfo->Token = NULL;
        PspClearCrossThreadFlag(Thread, CT_ACTIVE_IMPERSONATION_INFO_BIT);
    }

    PspUnlockThreadSecurityExclusive(Thread);

    if (Token != NULL) ObDereferenceObject(Token);

    PspWriteTebImpersonationInfo(Thread, THREAD_TO_PROCESS(Thread));
}

VOID
NTAPI
PsRevertToSelf(VOID)
{
    PAGED_CODE();

    PsRevertThreadToSelf(PsGetCurrentThread());
}

/*
 * Sets a thread's impersonation aside and hands the caller what was there,
 * so a driver can act as the process it is in and then put back exactly what
 * the thread was wearing. The reference moves to the caller.
 */
BOOLEAN
NTAPI
PsDisableImpersonation(
    _Inout_ PETHREAD Thread,
    _Inout_ PSE_IMPERSONATION_STATE ImpersonationState)
{
    PAGED_CODE();

    RtlZeroMemory(ImpersonationState, sizeof(*ImpersonationState));

    if (!Thread->ActiveImpersonationInfo) return FALSE;

    PspLockThreadSecurityExclusive(Thread);

    if (!Thread->ActiveImpersonationInfo)
    {
        PspUnlockThreadSecurityExclusive(Thread);
        return FALSE;
    }

    ImpersonationState->Token = Thread->ImpersonationInfo->Token;
    ImpersonationState->CopyOnOpen = Thread->ImpersonationInfo->CopyOnOpen;
    ImpersonationState->EffectiveOnly = Thread->ImpersonationInfo->EffectiveOnly;
    ImpersonationState->Level = Thread->ImpersonationInfo->ImpersonationLevel;

    Thread->ImpersonationInfo->Token = NULL;
    PspClearCrossThreadFlag(Thread, CT_ACTIVE_IMPERSONATION_INFO_BIT);

    PspUnlockThreadSecurityExclusive(Thread);

    PspWriteTebImpersonationInfo(Thread, THREAD_TO_PROCESS(Thread));

    return TRUE;
}

/*
 * Puts back what PsDisableImpersonation set aside. A state with no token in
 * it means the thread was not impersonating, so nothing is put back.
 */
VOID
NTAPI
PsRestoreImpersonation(
    _Inout_ PETHREAD Thread,
    _In_ PSE_IMPERSONATION_STATE ImpersonationState)
{
    PAGED_CODE();

    if (ImpersonationState->Token == NULL)
    {
        PsRevertThreadToSelf(Thread);
        return;
    }

    PspLockThreadSecurityExclusive(Thread);

    if (NT_SUCCESS(PspEnsureImpersonationInfo(Thread)))
    {
        PACCESS_TOKEN Old = Thread->ActiveImpersonationInfo ?
                                Thread->ImpersonationInfo->Token : NULL;

        Thread->ImpersonationInfo->Token = ImpersonationState->Token;
        Thread->ImpersonationInfo->CopyOnOpen = ImpersonationState->CopyOnOpen;
        Thread->ImpersonationInfo->EffectiveOnly =
            ImpersonationState->EffectiveOnly;
        Thread->ImpersonationInfo->ImpersonationLevel =
            ImpersonationState->Level;
        PspSetCrossThreadFlag(Thread, CT_ACTIVE_IMPERSONATION_INFO_BIT);

        PspUnlockThreadSecurityExclusive(Thread);

        if (Old != NULL) ObDereferenceObject(Old);
    }
    else
    {
        PspUnlockThreadSecurityExclusive(Thread);

        /* Nothing could be put back, so the token is given up instead of
         * being leaked onto a thread that cannot hold it */
        ObDereferenceObject(ImpersonationState->Token);
    }

    PspWriteTebImpersonationInfo(Thread, THREAD_TO_PROCESS(Thread));
}

/*
 * Makes a thread wear a token named by handle. Passing no handle takes off
 * whatever the thread is wearing.
 */
NTSTATUS
NTAPI
PsAssignImpersonationToken(
    _In_ PETHREAD Thread,
    _In_ HANDLE TokenHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    SECURITY_IMPERSONATION_LEVEL Level;
    PACCESS_TOKEN Token;
    NTSTATUS Status;

    PAGED_CODE();

    if (TokenHandle == NULL)
    {
        PsRevertThreadToSelf(Thread);
        return STATUS_SUCCESS;
    }

    Status = ObReferenceObjectByHandle(TokenHandle,
                                       TOKEN_IMPERSONATE,
                                       SeTokenObjectType,
                                       PreviousMode,
                                       &Token,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /* Only a token made for the purpose may be worn */
    if (SeTokenType(Token) != TokenImpersonation)
    {
        ObDereferenceObject(Token);
        return STATUS_BAD_TOKEN_TYPE;
    }

    Level = SeTokenImpersonationLevel(Token);

    Status = PsImpersonateClient(Thread, Token, FALSE, FALSE, Level);
    ObDereferenceObject(Token);

    return Status;
}

/*
 * Hands back the token a process runs with, named by a handle to the
 * process. The caller is given a reference it gives back itself.
 */
NTSTATUS
NTAPI
PsOpenTokenOfProcess(
    _In_ HANDLE ProcessHandle,
    _Outptr_ PACCESS_TOKEN *Token)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROCESS Process;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ProcessHandle,
                                       PROCESS_QUERY_INFORMATION,
                                       PsProcessType,
                                       PreviousMode,
                                       (PVOID*)&Process,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    *Token = PsReferencePrimaryToken(Process);
    ObDereferenceObject(Process);

    if (*Token == NULL) return STATUS_NO_TOKEN;

    return STATUS_SUCCESS;
}

/*
 * Turns on, or off, the redirection a process gets when it writes to places
 * it is not allowed to write to. Only a token that was made to allow this
 * may have it turned on, because the redirection is part of what the token
 * stands for and not something a caller may add to it.
 */
NTSTATUS
NTAPI
PspSetTokenVirtualization(
    _In_ PEPROCESS Process,
    _In_ BOOLEAN Enable)
{
    PTOKEN Token;

    PAGED_CODE();

    Token = PsReferencePrimaryToken(Process);
    if (Token == NULL) return STATUS_NO_TOKEN;

    SepAcquireTokenLockExclusive(Token);

    if ((Token->TokenFlags & TOKEN_VIRTUALIZE_ALLOWED) == 0)
    {
        SepReleaseTokenLock(Token);
        PsDereferencePrimaryToken(Token);
        return STATUS_ACCESS_DENIED;
    }

    if (Enable) Token->TokenFlags |= TOKEN_VIRTUALIZE_ENABLED;
    else Token->TokenFlags &= ~TOKEN_VIRTUALIZE_ENABLED;

    SepReleaseTokenLock(Token);
    PsDereferencePrimaryToken(Token);

    return STATUS_SUCCESS;
}

/* SYSTEM CALLS ***************************************************************/

/*
 * Opens the token a process runs with. The handle the caller gets names the
 * process's own token, not a copy, so changing it changes what the process
 * may do.
 */
NTSTATUS
NTAPI
NtOpenProcessTokenEx(
    _In_ HANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG HandleAttributes,
    _Out_ PHANDLE TokenHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PACCESS_TOKEN Token;
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

    Status = PsOpenTokenOfProcess(ProcessHandle, &Token);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObOpenObjectByPointer(Token,
                                   HandleAttributes,
                                   NULL,
                                   DesiredAccess,
                                   SeTokenObjectType,
                                   PreviousMode,
                                   &Handle);
    PsDereferencePrimaryToken(Token);

    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *TokenHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *TokenHandle = Handle;
    }

    return Status;
}

NTSTATUS
NTAPI
NtOpenProcessToken(
    _In_ HANDLE ProcessHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE TokenHandle)
{
    PAGED_CODE();

    return NtOpenProcessTokenEx(ProcessHandle,
                                DesiredAccess,
                                0,
                                TokenHandle);
}

/*
 * Makes one thread wear what another thread is wearing, at a level the
 * quality of service the caller passes decides.
 */
NTSTATUS
NTAPI
NtImpersonateThread(
    _In_ HANDLE ServerThreadHandle,
    _In_ HANDLE ClientThreadHandle,
    _In_ PSECURITY_QUALITY_OF_SERVICE SecurityQos)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    SECURITY_QUALITY_OF_SERVICE SafeQos;
    SECURITY_CLIENT_CONTEXT ClientContext;
    PETHREAD ServerThread;
    PETHREAD ClientThread;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(SecurityQos,
                         sizeof(SECURITY_QUALITY_OF_SERVICE),
                         sizeof(ULONG));
            SafeQos = *SecurityQos;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        SafeQos = *SecurityQos;
    }

    Status = ObReferenceObjectByHandle(ServerThreadHandle,
                                       THREAD_IMPERSONATE,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&ServerThread,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(ClientThreadHandle,
                                       THREAD_DIRECT_IMPERSONATION,
                                       PsThreadType,
                                       PreviousMode,
                                       (PVOID*)&ClientThread,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(ServerThread);
        return Status;
    }

    Status = SeCreateClientSecurity(ClientThread,
                                    &SafeQos,
                                    FALSE,
                                    &ClientContext);
    if (NT_SUCCESS(Status))
    {
        Status = SeImpersonateClientEx(&ClientContext, ServerThread);
        SeDeleteClientSecurity(&ClientContext);
    }

    ObDereferenceObject(ClientThread);
    ObDereferenceObject(ServerThread);

    return Status;
}
