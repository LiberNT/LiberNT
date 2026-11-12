/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Security subject context
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/*
 * One lock holds every captured context still, so a token cannot change while
 * a check is reading it. The security manager's start-up initialises it.
 */
ERESOURCE SepSubjectContextLock;

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Takes the security a thread acts with: the token it impersonates, if any,
 * and the token of the process behind it. Both are referenced, so the context
 * stays usable until it is released.
 */
VOID
NTAPI
SeCaptureSubjectContextEx(
    _In_opt_ PETHREAD Thread,
    _In_ PEPROCESS Process,
    _Out_ PSECURITY_SUBJECT_CONTEXT SubjectContext)
{
    BOOLEAN CopyOnOpen;
    BOOLEAN EffectiveOnly;

    PAGED_CODE();

    SubjectContext->ProcessAuditId = PsGetProcessId(Process);
    SubjectContext->PrimaryToken = (PACCESS_TOKEN)PsReferencePrimaryToken(Process);

    /* A thread that impersonates nobody acts with its process alone */
    SubjectContext->ClientToken = NULL;
    SubjectContext->ImpersonationLevel = SecurityAnonymous;

    if (Thread == NULL) return;

    SubjectContext->ClientToken =
        PsReferenceImpersonationToken(Thread,
                                      &CopyOnOpen,
                                      &EffectiveOnly,
                                      &SubjectContext->ImpersonationLevel);

    if (SubjectContext->ClientToken == NULL)
        SubjectContext->ImpersonationLevel = SecurityAnonymous;
}

VOID
NTAPI
SeCaptureSubjectContext(
    _Out_ PSECURITY_SUBJECT_CONTEXT SubjectContext)
{
    PAGED_CODE();

    SeCaptureSubjectContextEx(PsGetCurrentThread(),
                              PsGetCurrentProcess(),
                              SubjectContext);
}

VOID
NTAPI
SeLockSubjectContext(
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext)
{
    PAGED_CODE();

    /* The lock is not per context, so there is nothing in it to reach for */
    UNREFERENCED_PARAMETER(SubjectContext);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&SepSubjectContextLock, TRUE);
}

VOID
NTAPI
SeUnlockSubjectContext(
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(SubjectContext);

    ExReleaseResourceLite(&SepSubjectContextLock);
    KeLeaveCriticalRegion();
}

VOID
NTAPI
SeReleaseSubjectContext(
    _Inout_ PSECURITY_SUBJECT_CONTEXT SubjectContext)
{
    PAGED_CODE();

    if (SubjectContext->PrimaryToken != NULL)
    {
        PsDereferencePrimaryToken(SubjectContext->PrimaryToken);
        SubjectContext->PrimaryToken = NULL;
    }

    if (SubjectContext->ClientToken != NULL)
    {
        PsDereferenceImpersonationToken(SubjectContext->ClientToken);
        SubjectContext->ClientToken = NULL;
    }
}

/* EOF */
