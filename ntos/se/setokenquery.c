/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Reading what a token says and changing what may be changed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE DATA ***************************************************************/

/*
 * What a caller must hold over a token to ask one thing of it or to change
 * it, and how much room a fixed answer takes. An answer whose size depends on
 * the token itself is measured when it is asked for.
 */
typedef struct _SEP_TOKEN_QUESTION
{
    UCHAR Class;
    UCHAR Fixed;
    ACCESS_MASK ToAsk;
    ACCESS_MASK ToChange;
} SEP_TOKEN_QUESTION;

#define SEP_VARIES 0

static const SEP_TOKEN_QUESTION SepTokenQuestions[] =
{
    {TokenUser, SEP_VARIES, TOKEN_QUERY, 0},
    {TokenGroups, SEP_VARIES, TOKEN_QUERY, 0},
    {TokenPrivileges, SEP_VARIES, TOKEN_QUERY, 0},
    {TokenOwner, SEP_VARIES, TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenPrimaryGroup, SEP_VARIES, TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenDefaultDacl, SEP_VARIES, TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenSource, sizeof(TOKEN_SOURCE), TOKEN_QUERY_SOURCE, 0},
    {TokenType, sizeof(TOKEN_TYPE), TOKEN_QUERY, 0},
    {TokenImpersonationLevel, sizeof(SECURITY_IMPERSONATION_LEVEL), TOKEN_QUERY, 0},
    {TokenStatistics, sizeof(TOKEN_STATISTICS), TOKEN_QUERY, 0},
    {TokenRestrictedSids, SEP_VARIES, TOKEN_QUERY, 0},
    {TokenSessionId, sizeof(ULONG), TOKEN_QUERY, TOKEN_ADJUST_SESSIONID},
    {TokenGroupsAndPrivileges, SEP_VARIES, TOKEN_QUERY, 0},
    {TokenSessionReference, 0, 0, TOKEN_ADJUST_DEFAULT},
    {TokenSandBoxInert, sizeof(ULONG), TOKEN_QUERY, 0},
    {TokenAuditPolicy, SEP_VARIES, TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenOrigin, sizeof(TOKEN_ORIGIN), TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenElevationType, sizeof(ULONG), TOKEN_QUERY, 0},
    {TokenLinkedToken, sizeof(TOKEN_LINKED_TOKEN), TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenElevation, sizeof(TOKEN_ELEVATION), TOKEN_QUERY, 0},
    {TokenHasRestrictions, sizeof(ULONG), TOKEN_QUERY, 0},
    {TokenAccessInformation, SEP_VARIES, TOKEN_QUERY, 0},
    {TokenVirtualizationAllowed, sizeof(ULONG), TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenVirtualizationEnabled, sizeof(ULONG), TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenIntegrityLevel, SEP_VARIES, TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenUIAccess, sizeof(ULONG), TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenMandatoryPolicy, sizeof(TOKEN_MANDATORY_POLICY), TOKEN_QUERY, TOKEN_ADJUST_DEFAULT},
    {TokenLogonSid, SEP_VARIES, TOKEN_QUERY, 0},
    {TokenIsAppContainer, sizeof(ULONG), TOKEN_QUERY, 0},
    {TokenCapabilities, SEP_VARIES, TOKEN_QUERY, 0},
    {TokenAppContainerSid, SEP_VARIES, TOKEN_QUERY, 0},
    {TokenAppContainerNumber, sizeof(ULONG), TOKEN_QUERY, 0},
    {TokenIsRestricted, sizeof(ULONG), TOKEN_QUERY, 0},
    {TokenBnoIsolation, SEP_VARIES, TOKEN_QUERY, 0},
};

/* PRIVATE FUNCTIONS **********************************************************/

static
const SEP_TOKEN_QUESTION *
SepFindTokenQuestion(
    _In_ TOKEN_INFORMATION_CLASS Class)
{
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(SepTokenQuestions); i++)
    {
        if (SepTokenQuestions[i].Class == (UCHAR)Class)
            return &SepTokenQuestions[i];
    }

    return NULL;
}

/* The room an answer made of holders takes, headers and holders together */
static
ULONG
SepRoomForHolders(
    _In_ ULONG Header,
    _In_ ULONG Count,
    _In_reads_opt_(Count) PSID_AND_ATTRIBUTES Holders)
{
    ULONG Room = Header + Count * (ULONG)sizeof(SID_AND_ATTRIBUTES);
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        if (Holders[i].Sid != NULL) Room += RtlLengthSid(Holders[i].Sid);
    }

    return Room;
}

/*
 * Writes holders into the caller's own room, the headers where the caller
 * expects them and the holders after, each header pointing at its own.
 */
static
PUCHAR
SepAnswerWithHolders(
    _Out_writes_(Count) PSID_AND_ATTRIBUTES Headers,
    _In_ ULONG Count,
    _In_reads_opt_(Count) PSID_AND_ATTRIBUTES Holders,
    _In_ PUCHAR Bodies)
{
    ULONG Length;
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        Headers[i].Attributes = Holders[i].Attributes;

        if (Holders[i].Sid == NULL)
        {
            Headers[i].Sid = NULL;
            continue;
        }

        Length = RtlLengthSid(Holders[i].Sid);
        RtlCopyMemory(Bodies, Holders[i].Sid, Length);
        Headers[i].Sid = (PSID)Bodies;
        Bodies += Length;
    }

    return Bodies;
}

/* Writes one holder on its own, which is how an owner or a group is asked for */
static
ULONG
SepAnswerWithOneHolder(
    _Out_opt_ PVOID Buffer,
    _In_ ULONG Length,
    _In_opt_ PSID Sid,
    _Out_ PNTSTATUS Status)
{
    ULONG Room = (ULONG)sizeof(PSID) + ((Sid != NULL) ? RtlLengthSid(Sid) : 0);
    PSID *Named = Buffer;

    *Status = STATUS_SUCCESS;

    if (Buffer == NULL || Length < Room)
    {
        *Status = STATUS_BUFFER_TOO_SMALL;
        return Room;
    }

    if (Sid == NULL)
    {
        *Named = NULL;
        return Room;
    }

    *Named = (PSID)((PUCHAR)Buffer + sizeof(PSID));
    RtlCopyMemory(*Named, Sid, RtlLengthSid(Sid));

    return Room;
}

/*
 * Reads one thing out of a token into the caller's own room. The token is
 * held while it is read, so what comes back is what it said at one moment.
 */
static
NTSTATUS
SepAnswerAboutToken(
    _In_ PTOKEN Token,
    _In_ TOKEN_INFORMATION_CLASS Class,
    _Out_writes_bytes_opt_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Needed)
{
    NTSTATUS Status = STATUS_SUCCESS;
    const SEP_TOKEN_QUESTION *Question;
    ULONG Room;

    PAGED_CODE();

    Question = SepFindTokenQuestion(Class);
    if (Question == NULL || Question->ToAsk == 0)
        return STATUS_INVALID_INFO_CLASS;

    if (Question->Fixed != SEP_VARIES)
    {
        *Needed = Question->Fixed;
        if (Buffer == NULL || Length < Question->Fixed)
            return STATUS_BUFFER_TOO_SMALL;
    }

    switch (Class)
    {
        case TokenUser:
        {
            PTOKEN_USER Answer = Buffer;

            Room = SepRoomForHolders(sizeof(TOKEN_USER), 1, Token->UserAndGroups);
            *Needed = Room;

            if (Buffer == NULL || Length < Room) return STATUS_BUFFER_TOO_SMALL;

            SepAnswerWithHolders(&Answer->User,
                                 1,
                                 Token->UserAndGroups,
                                 (PUCHAR)Buffer + sizeof(TOKEN_USER));
            break;
        }

        case TokenGroups:
        {
            PTOKEN_GROUPS Answer = Buffer;
            ULONG Count = Token->UserAndGroupCount - 1;

            Room = SepRoomForHolders(FIELD_OFFSET(TOKEN_GROUPS, Groups),
                                     Count,
                                     &Token->UserAndGroups[1]);
            *Needed = Room;

            if (Buffer == NULL || Length < Room) return STATUS_BUFFER_TOO_SMALL;

            Answer->GroupCount = Count;
            SepAnswerWithHolders(Answer->Groups,
                                 Count,
                                 &Token->UserAndGroups[1],
                                 (PUCHAR)Answer->Groups +
                                     Count * sizeof(SID_AND_ATTRIBUTES));
            break;
        }

        case TokenRestrictedSids:
        {
            PTOKEN_GROUPS Answer = Buffer;

            Room = SepRoomForHolders(FIELD_OFFSET(TOKEN_GROUPS, Groups),
                                     Token->RestrictedSidCount,
                                     Token->RestrictedSids);
            *Needed = Room;

            if (Buffer == NULL || Length < Room) return STATUS_BUFFER_TOO_SMALL;

            Answer->GroupCount = Token->RestrictedSidCount;
            SepAnswerWithHolders(Answer->Groups,
                                 Token->RestrictedSidCount,
                                 Token->RestrictedSids,
                                 (PUCHAR)Answer->Groups +
                                     Token->RestrictedSidCount *
                                         sizeof(SID_AND_ATTRIBUTES));
            break;
        }

        case TokenCapabilities:
        {
            PTOKEN_GROUPS Answer = Buffer;

            /* TODO: a token holds no capabilities of its own to answer with */
            Room = FIELD_OFFSET(TOKEN_GROUPS, Groups);
            *Needed = Room;

            if (Buffer == NULL || Length < Room) return STATUS_BUFFER_TOO_SMALL;

            Answer->GroupCount = 0;
            break;
        }

        case TokenPrivileges:
        {
            PTOKEN_PRIVILEGES Answer = Buffer;

            Room = FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) +
                   Token->PrivilegeCount * (ULONG)sizeof(LUID_AND_ATTRIBUTES);
            *Needed = Room;

            if (Buffer == NULL || Length < Room) return STATUS_BUFFER_TOO_SMALL;

            Answer->PrivilegeCount = Token->PrivilegeCount;
            RtlCopyMemory(Answer->Privileges,
                          Token->Privileges,
                          Token->PrivilegeCount * sizeof(LUID_AND_ATTRIBUTES));
            break;
        }

        case TokenOwner:
            *Needed = SepAnswerWithOneHolder(
                Buffer,
                Length,
                Token->UserAndGroups[Token->DefaultOwnerIndex].Sid,
                &Status);
            break;

        case TokenPrimaryGroup:
            *Needed = SepAnswerWithOneHolder(Buffer,
                                             Length,
                                             Token->PrimaryGroup,
                                             &Status);
            break;

        case TokenAppContainerSid:
        {
            PSEP_LOWBOX_INFO Box = Token->LowBoxInfo;

            *Needed = SepAnswerWithOneHolder(Buffer,
                                             Length,
                                             (Box != NULL) ? Box->PackageSid : NULL,
                                             &Status);
            break;
        }

        case TokenLogonSid:
        {
            PTOKEN_GROUPS Answer = Buffer;
            ULONG i;

            /* The holder a logon is known by is the one the logon marked */
            for (i = 0; i < Token->UserAndGroupCount; i++)
            {
                if (Token->UserAndGroups[i].Attributes & SE_GROUP_LOGON_ID) break;
            }

            if (i == Token->UserAndGroupCount)
            {
                Room = FIELD_OFFSET(TOKEN_GROUPS, Groups);
                *Needed = Room;

                if (Buffer == NULL || Length < Room)
                    return STATUS_BUFFER_TOO_SMALL;

                Answer->GroupCount = 0;
                break;
            }

            Room = SepRoomForHolders(FIELD_OFFSET(TOKEN_GROUPS, Groups),
                                     1,
                                     &Token->UserAndGroups[i]);
            *Needed = Room;

            if (Buffer == NULL || Length < Room) return STATUS_BUFFER_TOO_SMALL;

            Answer->GroupCount = 1;
            SepAnswerWithHolders(Answer->Groups,
                                 1,
                                 &Token->UserAndGroups[i],
                                 (PUCHAR)Answer->Groups +
                                     sizeof(SID_AND_ATTRIBUTES));
            break;
        }

        case TokenIntegrityLevel:
        {
            PTOKEN_MANDATORY_LABEL Answer = Buffer;
            PSID Label = NULL;

            if (Token->IntegrityLevelIndex != MAXULONG)
                Label = Token->UserAndGroups[Token->IntegrityLevelIndex].Sid;

            Room = (ULONG)sizeof(TOKEN_MANDATORY_LABEL) +
                   ((Label != NULL) ? RtlLengthSid(Label) : 0);
            *Needed = Room;

            if (Buffer == NULL || Length < Room) return STATUS_BUFFER_TOO_SMALL;

            if (Label == NULL)
            {
                Answer->Label.Sid = NULL;
                Answer->Label.Attributes = 0;
                break;
            }

            Answer->Label.Attributes =
                Token->UserAndGroups[Token->IntegrityLevelIndex].Attributes;
            Answer->Label.Sid = (PSID)((PUCHAR)Buffer +
                                       sizeof(TOKEN_MANDATORY_LABEL));
            RtlCopyMemory(Answer->Label.Sid, Label, RtlLengthSid(Label));
            break;
        }

        case TokenDefaultDacl:
        {
            PTOKEN_DEFAULT_DACL Answer = Buffer;

            Room = (ULONG)sizeof(TOKEN_DEFAULT_DACL) +
                   ((Token->DefaultDacl != NULL) ? Token->DefaultDacl->AclSize : 0);
            *Needed = Room;

            if (Buffer == NULL || Length < Room) return STATUS_BUFFER_TOO_SMALL;

            if (Token->DefaultDacl == NULL)
            {
                Answer->DefaultDacl = NULL;
                break;
            }

            Answer->DefaultDacl = (PACL)((PUCHAR)Buffer +
                                         sizeof(TOKEN_DEFAULT_DACL));
            RtlCopyMemory(Answer->DefaultDacl,
                          Token->DefaultDacl,
                          Token->DefaultDacl->AclSize);
            break;
        }

        case TokenGroupsAndPrivileges:
        {
            PTOKEN_GROUPS_AND_PRIVILEGES Answer = Buffer;
            ULONG Groups = Token->UserAndGroupCount - 1;
            PUCHAR Bodies;

            Room = (ULONG)sizeof(TOKEN_GROUPS_AND_PRIVILEGES);
            Room += RtlLengthSid(Token->UserAndGroups[0].Sid);
            Room += Groups * (ULONG)sizeof(SID_AND_ATTRIBUTES);
            Room += SepRoomForHolders(0, Groups, &Token->UserAndGroups[1]);
            Room += SepRoomForHolders(0,
                                      Token->RestrictedSidCount,
                                      Token->RestrictedSids);
            Room += Token->PrivilegeCount * (ULONG)sizeof(LUID_AND_ATTRIBUTES);
            *Needed = Room;

            if (Buffer == NULL || Length < Room) return STATUS_BUFFER_TOO_SMALL;

            RtlZeroMemory(Answer, sizeof(*Answer));
            Bodies = (PUCHAR)Buffer + sizeof(TOKEN_GROUPS_AND_PRIVILEGES);

            Answer->SidCount = Groups;
            Answer->Sids = (PSID_AND_ATTRIBUTES)Bodies;
            Bodies += Groups * sizeof(SID_AND_ATTRIBUTES);
            Bodies = SepAnswerWithHolders(Answer->Sids,
                                          Groups,
                                          &Token->UserAndGroups[1],
                                          Bodies);
            Answer->SidLength = (ULONG)(Bodies - (PUCHAR)Answer->Sids);

            Answer->RestrictedSidCount = Token->RestrictedSidCount;
            if (Token->RestrictedSidCount != 0)
            {
                Answer->RestrictedSids = (PSID_AND_ATTRIBUTES)Bodies;
                Bodies += Token->RestrictedSidCount * sizeof(SID_AND_ATTRIBUTES);
                Bodies = SepAnswerWithHolders(Answer->RestrictedSids,
                                              Token->RestrictedSidCount,
                                              Token->RestrictedSids,
                                              Bodies);
                Answer->RestrictedSidLength =
                    (ULONG)(Bodies - (PUCHAR)Answer->RestrictedSids);
            }

            Answer->PrivilegeCount = Token->PrivilegeCount;
            if (Token->PrivilegeCount != 0)
            {
                Answer->Privileges = (PLUID_AND_ATTRIBUTES)Bodies;
                RtlCopyMemory(Answer->Privileges,
                              Token->Privileges,
                              Token->PrivilegeCount * sizeof(LUID_AND_ATTRIBUTES));
                Answer->PrivilegeLength =
                    Token->PrivilegeCount * (ULONG)sizeof(LUID_AND_ATTRIBUTES);
            }

            Answer->AuthenticationId = Token->AuthenticationId;
            break;
        }

        case TokenStatistics:
        {
            PTOKEN_STATISTICS Answer = Buffer;

            Answer->TokenId = Token->TokenId;
            Answer->AuthenticationId = Token->AuthenticationId;
            Answer->ExpirationTime = Token->ExpirationTime;
            Answer->TokenType = Token->TokenType;
            Answer->ImpersonationLevel = Token->ImpersonationLevel;
            Answer->DynamicCharged = Token->DynamicCharged;
            Answer->DynamicAvailable = Token->DynamicAvailable;
            Answer->GroupCount = Token->UserAndGroupCount - 1;
            Answer->PrivilegeCount = Token->PrivilegeCount;
            Answer->ModifiedId = Token->ModifiedId;
            break;
        }

        case TokenSource:
            RtlCopyMemory(Buffer, &Token->TokenSource, sizeof(TOKEN_SOURCE));
            break;

        case TokenType:
            *(PTOKEN_TYPE)Buffer = Token->TokenType;
            break;

        case TokenImpersonationLevel:
            if (Token->TokenType != TokenImpersonation)
                return STATUS_INVALID_INFO_CLASS;

            *(PSECURITY_IMPERSONATION_LEVEL)Buffer = Token->ImpersonationLevel;
            break;

        case TokenSessionId:
            *(PULONG)Buffer = Token->SessionId;
            break;

        case TokenSandBoxInert:
            *(PULONG)Buffer = (Token->TokenFlags & TOKEN_SANDBOX_INERT) ? 1 : 0;
            break;

        case TokenHasRestrictions:
            *(PULONG)Buffer = (Token->RestrictedSidCount != 0) ? 1 : 0;
            break;

        case TokenIsRestricted:
            *(PULONG)Buffer = (Token->TokenFlags & TOKEN_IS_RESTRICTED) ? 1 : 0;
            break;

        case TokenVirtualizationAllowed:
            *(PULONG)Buffer =
                (Token->TokenFlags & TOKEN_VIRTUALIZE_ALLOWED) ? 1 : 0;
            break;

        case TokenVirtualizationEnabled:
            *(PULONG)Buffer =
                (Token->TokenFlags & TOKEN_VIRTUALIZE_ENABLED) ? 1 : 0;
            break;

        case TokenUIAccess:
            *(PULONG)Buffer = (Token->TokenFlags & TOKEN_UIACCESS) ? 1 : 0;
            break;

        case TokenIsAppContainer:
            *(PULONG)Buffer = (Token->TokenFlags & TOKEN_LOWBOX) ? 1 : 0;
            break;

        case TokenAppContainerNumber:
        {
            PSEP_LOWBOX_INFO Box = Token->LowBoxInfo;

            *(PULONG)Buffer = (Box != NULL) ? Box->LowBoxNumber : 0;
            break;
        }

        case TokenMandatoryPolicy:
            ((PTOKEN_MANDATORY_POLICY)Buffer)->Policy = Token->MandatoryPolicy;
            break;

        case TokenOrigin:
            ((PTOKEN_ORIGIN)Buffer)->OriginatingLogonSession =
                Token->OriginatingLogonSession;
            break;

        case TokenElevationType:
            /* TODO: a token is never one of a pair, so it stands on its own */
            *(PULONG)Buffer = 1;
            break;

        case TokenElevation:
            ((PTOKEN_ELEVATION)Buffer)->TokenIsElevated =
                SeTokenIsAdmin(Token) ? 1 : 0;
            break;

        case TokenLinkedToken:
            /* TODO: no token is linked to another, none being made in pairs */
            ((PTOKEN_LINKED_TOKEN)Buffer)->LinkedToken = NULL;
            return STATUS_NOT_FOUND;

        case TokenAuditPolicy:
            /* TODO: what a token has audited is not kept */
            *Needed = 0;
            return STATUS_NOT_IMPLEMENTED;

        case TokenBnoIsolation:
        {
            PTOKEN_BNO_ISOLATION_INFORMATION Answer = Buffer;

            Room = (ULONG)sizeof(*Answer) + Token->BnoIsolationPrefix.Length +
                   (ULONG)sizeof(WCHAR);
            *Needed = Room;

            if (Buffer == NULL || Length < Room) return STATUS_BUFFER_TOO_SMALL;

            RtlZeroMemory(Buffer, Room);

            if (Token->BnoIsolationPrefix.Length == 0)
            {
                Answer->IsolationEnabled = FALSE;
                break;
            }

            Answer->IsolationEnabled = TRUE;
            Answer->IsolationPrefix = (PWSTR)((PUCHAR)Buffer + sizeof(*Answer));
            RtlCopyMemory(Answer->IsolationPrefix,
                          Token->BnoIsolationPrefix.Buffer,
                          Token->BnoIsolationPrefix.Length);
            break;
        }

        case TokenAccessInformation:
            /* TODO: the gathered form of what a token says is not built */
            *Needed = sizeof(TOKEN_ACCESS_INFORMATION);
            return STATUS_NOT_IMPLEMENTED;

        default:
            return STATUS_INVALID_INFO_CLASS;
    }

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

TOKEN_TYPE
NTAPI
SeTokenType(
    _In_ PACCESS_TOKEN Token)
{
    return ((PTOKEN)Token)->TokenType;
}

SECURITY_IMPERSONATION_LEVEL
NTAPI
SeTokenImpersonationLevel(
    _In_ PACCESS_TOKEN Token)
{
    return ((PTOKEN)Token)->ImpersonationLevel;
}

BOOLEAN
NTAPI
SeTokenIsRestricted(
    _In_ PACCESS_TOKEN Token)
{
    return (((PTOKEN)Token)->TokenFlags & TOKEN_IS_RESTRICTED) != 0;
}

BOOLEAN
NTAPI
SeTokenIsWriteRestricted(
    _In_ PACCESS_TOKEN Token)
{
    return (((PTOKEN)Token)->TokenFlags & TOKEN_WRITE_RESTRICTED) != 0;
}

BOOLEAN
NTAPI
SeTokenIsInert(
    _In_ PTOKEN Token)
{
    return (Token->TokenFlags & TOKEN_SANDBOX_INERT) != 0;
}

/* Whether the token speaks for somebody who may administer the system */
BOOLEAN
NTAPI
SeTokenIsAdmin(
    _In_ PACCESS_TOKEN _Token)
{
    PTOKEN Token = _Token;
    ULONG i;

    for (i = 0; i < Token->UserAndGroupCount; i++)
    {
        if (Token->UserAndGroups[i].Sid == NULL) continue;
        if (!RtlEqualSid(Token->UserAndGroups[i].Sid, SeAliasAdminsSid)) continue;

        /* A holder that is only counted against grants nothing */
        if (Token->UserAndGroups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY)
            return FALSE;

        return (Token->UserAndGroups[i].Attributes & SE_GROUP_ENABLED) != 0;
    }

    return FALSE;
}

NTSTATUS
NTAPI
SeQueryAuthenticationIdToken(
    _In_ PACCESS_TOKEN Token,
    _Out_ PLUID AuthenticationId)
{
    *AuthenticationId = ((PTOKEN)Token)->AuthenticationId;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
SeQuerySessionIdToken(
    _In_ PACCESS_TOKEN Token,
    _Out_ PULONG SessionId)
{
    PTOKEN Real = Token;

    SepAcquireTokenLockShared(Real);
    *SessionId = Real->SessionId;
    SepReleaseTokenLock(Real);

    return STATUS_SUCCESS;
}

/* What a token is known by, so that two of them can be told apart */
VOID
NTAPI
SeGetTokenControlInformation(
    _In_ PACCESS_TOKEN _Token,
    _Out_ PTOKEN_CONTROL TokenControl)
{
    PTOKEN Token = _Token;

    SepAcquireTokenLockShared(Token);

    TokenControl->TokenId = Token->TokenId;
    TokenControl->AuthenticationId = Token->AuthenticationId;
    TokenControl->ModifiedId = Token->ModifiedId;
    TokenControl->TokenSource = Token->TokenSource;

    SepReleaseTokenLock(Token);
}

BOOLEAN
NTAPI
SeSecurityAttributePresent(
    _In_ PACCESS_TOKEN Token,
    _In_ PUNICODE_STRING AttributeName,
    _In_ ULONG SidSet)
{
    UNREFERENCED_PARAMETER(Token);
    UNREFERENCED_PARAMETER(AttributeName);
    UNREFERENCED_PARAMETER(SidSet);

    /* TODO: a token carries no attributes of its own to be asked about */
    return FALSE;
}

/*
 * Reads one thing out of a token into room the kernel takes for the caller,
 * which is how a driver asks without having to measure first.
 */
NTSTATUS
NTAPI
SeQueryInformationToken(
    _In_ PACCESS_TOKEN Token,
    _In_ TOKEN_INFORMATION_CLASS TokenInformationClass,
    _Outptr_result_buffer_(_Inexpressible_(token-dependent)) PVOID *TokenInformation)
{
    PTOKEN Real = Token;
    PVOID Answer;
    ULONG Needed = 0;
    NTSTATUS Status;

    PAGED_CODE();

    *TokenInformation = NULL;

    SepAcquireTokenLockShared(Real);

    Status = SepAnswerAboutToken(Real,
                                 TokenInformationClass,
                                 NULL,
                                 0,
                                 &Needed);
    if (Status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(Status))
    {
        SepReleaseTokenLock(Real);
        return Status;
    }

    Answer = ExAllocatePoolWithTag(PagedPool, Needed, TAG_SE);
    if (Answer == NULL)
    {
        SepReleaseTokenLock(Real);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = SepAnswerAboutToken(Real,
                                 TokenInformationClass,
                                 Answer,
                                 Needed,
                                 &Needed);

    SepReleaseTokenLock(Real);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Answer, TAG_SE);
        return Status;
    }

    *TokenInformation = Answer;
    return STATUS_SUCCESS;
}

/* SYSTEM CALLS ***************************************************************/

__kernel_entry
NTSTATUS
NTAPI
NtQueryInformationToken(
    _In_ HANDLE TokenHandle,
    _In_ TOKEN_INFORMATION_CLASS TokenInformationClass,
    _Out_writes_bytes_to_opt_(TokenInformationLength, *ReturnLength) PVOID TokenInformation,
    _In_ ULONG TokenInformationLength,
    _Out_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    const SEP_TOKEN_QUESTION *Question;
    PTOKEN Token;
    PVOID Room = NULL;
    ULONG Needed = 0;
    NTSTATUS Status;

    PAGED_CODE();

    Question = SepFindTokenQuestion(TokenInformationClass);
    if (Question == NULL || Question->ToAsk == 0)
        return STATUS_INVALID_INFO_CLASS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (TokenInformationLength != 0)
            {
                ProbeForWrite(TokenInformation,
                              TokenInformationLength,
                              sizeof(ULONG));
            }

            ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(TokenHandle,
                                       Question->ToAsk,
                                       SeTokenObjectType,
                                       PreviousMode,
                                       (PVOID *)&Token,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * The answer is built where it cannot fault, because a token may not be
     * held while the caller's own memory is written to.
     */
    if (TokenInformationLength != 0)
    {
        Room = ExAllocatePoolWithTag(PagedPool, TokenInformationLength, TAG_SE);
        if (Room == NULL)
        {
            ObDereferenceObject(Token);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    SepAcquireTokenLockShared(Token);

    Status = SepAnswerAboutToken(Token,
                                 TokenInformationClass,
                                 Room,
                                 TokenInformationLength,
                                 &Needed);

    SepReleaseTokenLock(Token);
    ObDereferenceObject(Token);

    _SEH2_TRY
    {
        *ReturnLength = Needed;

        if (NT_SUCCESS(Status) && Room != NULL)
            RtlCopyMemory(TokenInformation, Room, Needed);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (Room != NULL) ExFreePoolWithTag(Room, TAG_SE);

    return Status;
}

/*
 * Changes one of the few things about a token that may be changed once it
 * exists. What a token says about who it speaks for never can.
 */
__kernel_entry
NTSTATUS
NTAPI
NtSetInformationToken(
    _In_ HANDLE TokenHandle,
    _In_ TOKEN_INFORMATION_CLASS TokenInformationClass,
    _In_reads_bytes_(TokenInformationLength) PVOID TokenInformation,
    _In_ ULONG TokenInformationLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    const SEP_TOKEN_QUESTION *Question;
    PTOKEN Token;
    PVOID Given = NULL;
    PSID Named = NULL;
    PACL Dacl = NULL;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    Question = SepFindTokenQuestion(TokenInformationClass);
    if (Question == NULL || Question->ToChange == 0)
        return STATUS_INVALID_INFO_CLASS;

    if (Question->Fixed != SEP_VARIES && TokenInformationLength < Question->Fixed)
        return STATUS_INFO_LENGTH_MISMATCH;

    /* What the caller says is copied before the token is held */
    if (TokenInformationLength != 0)
    {
        Given = ExAllocatePoolWithTag(PagedPool, TokenInformationLength, TAG_SE);
        if (Given == NULL) return STATUS_INSUFFICIENT_RESOURCES;

        _SEH2_TRY
        {
            if (PreviousMode != KernelMode)
            {
                ProbeForRead(TokenInformation,
                             TokenInformationLength,
                             sizeof(ULONG));
            }

            RtlCopyMemory(Given, TokenInformation, TokenInformationLength);
            Status = STATUS_SUCCESS;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Given, TAG_SE);
            return Status;
        }
    }

    Status = ObReferenceObjectByHandle(TokenHandle,
                                       Question->ToChange,
                                       SeTokenObjectType,
                                       PreviousMode,
                                       (PVOID *)&Token,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        if (Given != NULL) ExFreePoolWithTag(Given, TAG_SE);
        return Status;
    }

    SepAcquireTokenLockExclusive(Token);

    switch (TokenInformationClass)
    {
        case TokenOwner:
        {
            Named = ((PTOKEN_OWNER)Given)->Owner;
            Status = STATUS_INVALID_OWNER;

            /* Only somebody the token already speaks for may own what it makes */
            for (i = 0; i < Token->UserAndGroupCount; i++)
            {
                if (Token->UserAndGroups[i].Sid == NULL) continue;
                if (!RtlEqualSid(Token->UserAndGroups[i].Sid, Named)) continue;
                if (Token->UserAndGroups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY)
                    continue;

                Token->DefaultOwnerIndex = i;
                Status = STATUS_SUCCESS;
                break;
            }
            break;
        }

        case TokenPrimaryGroup:
        {
            ULONG Room;

            Named = ((PTOKEN_PRIMARY_GROUP)Given)->PrimaryGroup;
            if (Named == NULL || !RtlValidSid(Named))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Room = RtlLengthSid(Named);

            Status = SepGrowChangeablePart(Token, Room);
            if (!NT_SUCCESS(Status)) break;

            RtlCopyMemory((PUCHAR)Token->DynamicPart + Token->DynamicCharged -
                              Token->DynamicAvailable,
                          Named,
                          Room);
            Token->PrimaryGroup = (PSID)((PUCHAR)Token->DynamicPart +
                                         Token->DynamicCharged -
                                         Token->DynamicAvailable);
            Token->DynamicAvailable -= Room;
            break;
        }

        case TokenDefaultDacl:
        {
            Dacl = ((PTOKEN_DEFAULT_DACL)Given)->DefaultDacl;

            if (Dacl != NULL && !RtlValidAcl(Dacl))
            {
                Status = STATUS_INVALID_ACL;
                break;
            }

            Status = SepGrowChangeablePart(Token,
                                           (Dacl != NULL) ? Dacl->AclSize : 0);
            if (!NT_SUCCESS(Status)) break;

            if (Dacl == NULL)
            {
                Token->DefaultDacl = NULL;
                break;
            }

            RtlCopyMemory((PUCHAR)Token->DynamicPart + Token->DynamicCharged -
                              Token->DynamicAvailable,
                          Dacl,
                          Dacl->AclSize);
            Token->DefaultDacl = (PACL)((PUCHAR)Token->DynamicPart +
                                        Token->DynamicCharged -
                                        Token->DynamicAvailable);
            Token->DynamicAvailable -= Dacl->AclSize;
            break;
        }

        case TokenSessionId:
            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Token->SessionId = *(PULONG)Given;
            break;

        case TokenSessionReference:
            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            /* TODO: a logon is not held open on a token's behalf */
            break;

        case TokenOrigin:
            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Token->OriginatingLogonSession =
                ((PTOKEN_ORIGIN)Given)->OriginatingLogonSession;
            break;

        case TokenMandatoryPolicy:
            if (!SeSinglePrivilegeCheck(SeRelabelPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            Token->MandatoryPolicy =
                ((PTOKEN_MANDATORY_POLICY)Given)->Policy;
            break;

        case TokenVirtualizationAllowed:
            if (*(PULONG)Given != 0) Token->TokenFlags |= TOKEN_VIRTUALIZE_ALLOWED;
            else Token->TokenFlags &= ~TOKEN_VIRTUALIZE_ALLOWED;
            break;

        case TokenVirtualizationEnabled:
            if (*(PULONG)Given != 0) Token->TokenFlags |= TOKEN_VIRTUALIZE_ENABLED;
            else Token->TokenFlags &= ~TOKEN_VIRTUALIZE_ENABLED;
            break;

        case TokenUIAccess:
            if (!SeSinglePrivilegeCheck(SeTcbPrivilege, PreviousMode))
            {
                Status = STATUS_PRIVILEGE_NOT_HELD;
                break;
            }

            if (*(PULONG)Given != 0) Token->TokenFlags |= TOKEN_UIACCESS;
            else Token->TokenFlags &= ~TOKEN_UIACCESS;
            break;

        case TokenIntegrityLevel:
        {
            PTOKEN_MANDATORY_LABEL Label = Given;
            ULONG Asked;

            if (Label->Label.Sid == NULL || !RtlValidSid(Label->Label.Sid))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (Token->IntegrityLevelIndex == MAXULONG)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Asked = *RtlSubAuthoritySid(Label->Label.Sid,
                                        *RtlSubAuthorityCountSid(Label->Label.Sid) - 1);

            /* Nobody may make itself more trusted than it already is */
            if (Asked > SepGetTokenIntegrityRid(Token))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Asked = RtlLengthSid(Token->UserAndGroups[Token->IntegrityLevelIndex].Sid);
            if (Asked != RtlLengthSid(Label->Label.Sid))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            RtlCopyMemory(Token->UserAndGroups[Token->IntegrityLevelIndex].Sid,
                          Label->Label.Sid,
                          Asked);
            break;
        }

        case TokenLinkedToken:
            /* TODO: tokens are not made in pairs, so none can be linked */
            Status = STATUS_NOT_IMPLEMENTED;
            break;

        case TokenAuditPolicy:
            /* TODO: what a token is to have audited is not kept */
            Status = STATUS_NOT_IMPLEMENTED;
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    if (NT_SUCCESS(Status)) ExAllocateLocallyUniqueId(&Token->ModifiedId);

    SepReleaseTokenLock(Token);
    ObDereferenceObject(Token);

    if (Given != NULL) ExFreePoolWithTag(Given, TAG_SE);

    return Status;
}
