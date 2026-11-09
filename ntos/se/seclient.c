/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Client security contexts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* The published header turns the delete into a macro, so the exported
 * function has to be defined without it */
#undef SeDeleteClientSecurity

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Builds what a server needs to act as a client later. Static tracking takes
 * a snapshot the server then owns; dynamic tracking keeps the client's own
 * token, so the server follows whatever the client becomes.
 */
static
NTSTATUS
SepCreateClientSecurity(
    _In_ PACCESS_TOKEN Token,
    _In_ SECURITY_IMPERSONATION_LEVEL ClientLevel,
    _In_ PSECURITY_QUALITY_OF_SERVICE ClientSecurityQos,
    _In_ BOOLEAN ServerIsRemote,
    _Out_ PSECURITY_CLIENT_CONTEXT ClientContext)
{
    PTOKEN Identity = (PTOKEN)Token;
    PACCESS_TOKEN Snapshot;
    NTSTATUS Status;

    PAGED_CODE();

    ClientContext->SecurityQos = *ClientSecurityQos;
    ClientContext->ServerIsRemote = ServerIsRemote;
    ClientContext->ClientToken = NULL;
    ClientContext->DirectlyAccessClientToken = FALSE;
    ClientContext->DirectAccessEffectiveOnly = FALSE;
    RtlZeroMemory(&ClientContext->ClientTokenControl, sizeof(TOKEN_CONTROL));

    /* A client that is itself impersonating lends only the level it holds */
    if (SeTokenType(Token) == TokenImpersonation &&
        ClientSecurityQos->ImpersonationLevel > ClientLevel)
    {
        return STATUS_BAD_IMPERSONATION_LEVEL;
    }

    /*
     * A remote server has to be able to tell later whether it is still the
     * same token, so its identity travels with the context.
     */
    /* TODO: the identity is read without the token's own lock */
    if (ServerIsRemote)
    {
        ClientContext->ClientTokenControl.TokenId = Identity->TokenId;
        ClientContext->ClientTokenControl.AuthenticationId =
            Identity->AuthenticationId;
        ClientContext->ClientTokenControl.ModifiedId = Identity->ModifiedId;
        ClientContext->ClientTokenControl.TokenSource = Identity->TokenSource;
    }

    if (ClientSecurityQos->ContextTrackingMode == SECURITY_DYNAMIC_TRACKING)
    {
        /* The context holds the client's token itself, so it takes its own
         * reference and the caller keeps none of ours */
        ObReferenceObject(Token);

        ClientContext->ClientToken = Token;
        ClientContext->DirectlyAccessClientToken = TRUE;
        ClientContext->DirectAccessEffectiveOnly =
            ClientSecurityQos->EffectiveOnly;

        return STATUS_SUCCESS;
    }

    Status = SeCopyClientToken(Token,
                               ClientSecurityQos->ImpersonationLevel,
                               KernelMode,
                               &Snapshot);
    if (!NT_SUCCESS(Status)) return Status;

    ClientContext->ClientToken = Snapshot;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

NTSTATUS
NTAPI
SeCreateClientSecurity(
    _In_ PETHREAD ClientThread,
    _In_ PSECURITY_QUALITY_OF_SERVICE ClientSecurityQos,
    _In_ BOOLEAN RemoteSession,
    _Out_ PSECURITY_CLIENT_CONTEXT ClientContext)
{
    SECURITY_IMPERSONATION_LEVEL ClientLevel = SecurityAnonymous;
    BOOLEAN CopyOnOpen;
    BOOLEAN EffectiveOnly;
    PACCESS_TOKEN Token;
    NTSTATUS Status;

    PAGED_CODE();

    Token = PsReferenceImpersonationToken(ClientThread,
                                          &CopyOnOpen,
                                          &EffectiveOnly,
                                          &ClientLevel);
    if (Token == NULL)
    {
        /* A client impersonating nobody acts as its own process */
        Token = (PACCESS_TOKEN)PsReferencePrimaryToken(
                    PsGetThreadProcess(ClientThread));
    }

    Status = SepCreateClientSecurity(Token,
                                     ClientLevel,
                                     ClientSecurityQos,
                                     RemoteSession,
                                     ClientContext);

    /* Whatever the context kept, it referenced for itself */
    PsDereferencePrimaryToken(Token);

    return Status;
}

NTSTATUS
NTAPI
SeCreateClientSecurityFromSubjectContext(
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ PSECURITY_QUALITY_OF_SERVICE ClientSecurityQos,
    _In_ BOOLEAN ServerIsRemote,
    _Out_ PSECURITY_CLIENT_CONTEXT ClientContext)
{
    PACCESS_TOKEN Token;

    PAGED_CODE();

    /* The context answers with what it impersonates, or its process token */
    Token = SeQuerySubjectContextToken(SubjectContext);
    if (Token == NULL) return STATUS_NO_TOKEN;

    return SepCreateClientSecurity(Token,
                                   SubjectContext->ImpersonationLevel,
                                   ClientSecurityQos,
                                   ServerIsRemote,
                                   ClientContext);
}

NTSTATUS
NTAPI
SeImpersonateClientEx(
    _In_ PSECURITY_CLIENT_CONTEXT ClientContext,
    _In_opt_ PETHREAD ServerThread)
{
    SECURITY_IMPERSONATION_LEVEL Level;
    BOOLEAN EffectiveOnly;

    PAGED_CODE();

    if (ServerThread == NULL) ServerThread = PsGetCurrentThread();

    if (ClientContext->DirectlyAccessClientToken)
    {
        /*
         * The token belongs to the client, so the level it is at now is what
         * the server gets, and opening it has to yield a copy rather than the
         * client's own token.
         */
        Level = SeTokenImpersonationLevel(ClientContext->ClientToken);
        EffectiveOnly = ClientContext->DirectAccessEffectiveOnly;
    }
    else
    {
        /* The snapshot is already fixed at the level it was taken for */
        Level = ClientContext->SecurityQos.ImpersonationLevel;
        EffectiveOnly = ClientContext->SecurityQos.EffectiveOnly;
    }

    return PsImpersonateClient(ServerThread,
                               ClientContext->ClientToken,
                               ClientContext->DirectlyAccessClientToken,
                               EffectiveOnly,
                               Level);
}

VOID
NTAPI
SeImpersonateClient(
    _In_ PSECURITY_CLIENT_CONTEXT ClientContext,
    _In_opt_ PETHREAD ServerThread)
{
    PAGED_CODE();

    (VOID)SeImpersonateClientEx(ClientContext, ServerThread);
}

VOID
NTAPI
SeDeleteClientSecurity(
    _Inout_ PSECURITY_CLIENT_CONTEXT ClientContext)
{
    PAGED_CODE();

    if (ClientContext->ClientToken == NULL) return;

    /* The two token dereferences are the same entry point, so a snapshot and
     * a client's own token are given back the same way */
    PsDereferencePrimaryToken(ClientContext->ClientToken);
    ClientContext->ClientToken = NULL;
}

/* EOF */
