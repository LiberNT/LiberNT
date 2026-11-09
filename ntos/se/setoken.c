/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     What a token is made of and how one comes to be
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE SeTokenObjectType = NULL;

TOKEN_SOURCE SeSystemTokenSource = {{'*', 'S', 'Y', 'S', 'T', 'E', 'M', '*'}, {0, 0}};
LUID SeSystemAuthenticationId = SYSTEM_LUID;
LUID SeAnonymousAuthenticationId = ANONYMOUS_LOGON_LUID;

/* PRIVATE DATA ***************************************************************/

/*
 * Everything a token is to say, gathered before the token exists. The arrays
 * are read once and copied into the token's own room, so whoever handed them
 * in cannot change what the token says afterwards.
 */
typedef struct _SEP_TOKEN_CONTENTS
{
    PSID_AND_ATTRIBUTES UserAndGroups;
    ULONG UserAndGroupCount;
    PSID_AND_ATTRIBUTES RestrictedSids;
    ULONG RestrictedSidCount;
    PLUID_AND_ATTRIBUTES Privileges;
    ULONG PrivilegeCount;
    PSID PrimaryGroup;
    PACL DefaultDacl;
    ULONG DefaultOwnerIndex;
    LUID AuthenticationId;
    LUID OriginatingLogonSession;
    LUID ParentTokenId;
    LARGE_INTEGER ExpirationTime;
    TOKEN_SOURCE Source;
    TOKEN_TYPE Type;
    SECURITY_IMPERSONATION_LEVEL Level;
    ULONG SessionId;
    ULONG MandatoryPolicy;
    ULONG Flags;
} SEP_TOKEN_CONTENTS, *PSEP_TOKEN_CONTENTS;

/* One privilege a token the system makes for itself is to hold */
typedef struct _SEP_SYSTEM_PRIVILEGE
{
    const LUID *Privilege;
    ULONG Attributes;
} SEP_SYSTEM_PRIVILEGE;

#define SEP_PRIVILEGE_ON (SE_PRIVILEGE_ENABLED | SE_PRIVILEGE_ENABLED_BY_DEFAULT)

#define SEP_GROUP_ON                                                          \
    (SE_GROUP_ENABLED | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_MANDATORY)

static GENERIC_MAPPING SepTokenMapping =
{
    TOKEN_READ,
    TOKEN_WRITE,
    TOKEN_EXECUTE,
    TOKEN_ALL_ACCESS
};

/* PRIVATE FUNCTIONS **********************************************************/

/* The room an array of holders takes, headers and holders together */
static
ULONG
SepRoomForSids(
    _In_ ULONG Count,
    _In_reads_opt_(Count) PSID_AND_ATTRIBUTES Array)
{
    ULONG Room = Count * (ULONG)sizeof(SID_AND_ATTRIBUTES);
    ULONG i;

    for (i = 0; i < Count; i++)
    {
        if (Array[i].Sid != NULL) Room += RtlLengthSid(Array[i].Sid);
    }

    return Room;
}

/*
 * Writes an array of holders into room the token owns, the headers first and
 * then the holders themselves, so the whole array is one block the token can
 * hand out without anybody reaching outside it.
 */
static
PUCHAR
SepWriteSids(
    _Out_ PSID_AND_ATTRIBUTES *Written,
    _In_ ULONG Count,
    _In_reads_opt_(Count) PSID_AND_ATTRIBUTES Array,
    _In_ PUCHAR Room)
{
    PSID_AND_ATTRIBUTES Headers = (PSID_AND_ATTRIBUTES)Room;
    PUCHAR Bodies = Room + Count * sizeof(SID_AND_ATTRIBUTES);
    ULONG Length;
    ULONG i;

    *Written = (Count != 0) ? Headers : NULL;

    for (i = 0; i < Count; i++)
    {
        Headers[i].Attributes = Array[i].Attributes;

        if (Array[i].Sid == NULL)
        {
            Headers[i].Sid = NULL;
            continue;
        }

        Length = RtlLengthSid(Array[i].Sid);
        RtlCopyMemory(Bodies, Array[i].Sid, Length);
        Headers[i].Sid = (PSID)Bodies;
        Bodies += Length;
    }

    return Bodies;
}

/* The two things a token keeps apart from the rest, because they can change */
static
NTSTATUS
SepWriteChangeablePart(
    _Inout_ PTOKEN Token,
    _In_opt_ PSID PrimaryGroup,
    _In_opt_ PACL DefaultDacl,
    _In_ ULONG Spare)
{
    ULONG GroupLength = (PrimaryGroup != NULL) ? RtlLengthSid(PrimaryGroup) : 0;
    ULONG DaclLength = (DefaultDacl != NULL) ? DefaultDacl->AclSize : 0;
    ULONG Room = ALIGN_UP(GroupLength + DaclLength + Spare, ULONG);
    PUCHAR Where;

    if (Room == 0) Room = sizeof(ULONG);

    Token->DynamicPart = ExAllocatePoolWithTag(PagedPool, Room, TAG_SE);
    if (Token->DynamicPart == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Token->DynamicPart, Room);

    Token->DynamicCharged = Room;
    Token->DynamicAvailable = Room - GroupLength - DaclLength;

    Where = (PUCHAR)Token->DynamicPart;

    if (GroupLength != 0)
    {
        RtlCopyMemory(Where, PrimaryGroup, GroupLength);
        Token->PrimaryGroup = (PSID)Where;
        Where += GroupLength;
    }
    else
    {
        Token->PrimaryGroup = NULL;
    }

    if (DaclLength != 0)
    {
        RtlCopyMemory(Where, DefaultDacl, DaclLength);
        Token->DefaultDacl = (PACL)Where;
    }
    else
    {
        Token->DefaultDacl = NULL;
    }

    return STATUS_SUCCESS;
}

/*
 * Reads the privileges a token holds and remembers the few the rest of the
 * system asks about often enough that walking the array each time would show.
 */
VOID
SepNoteTokenPrivilegeFlags(
    _Inout_ PTOKEN Token)
{
    ULONG Flags = 0;
    ULONG i;

    for (i = 0; i < Token->PrivilegeCount; i++)
    {
        if (!(Token->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED)) continue;

        if (RtlEqualLuid(&Token->Privileges[i].Luid, &SeChangeNotifyPrivilege))
            Flags |= TOKEN_HAS_TRAVERSE_PRIVILEGE;
        else if (RtlEqualLuid(&Token->Privileges[i].Luid, &SeBackupPrivilege))
            Flags |= TOKEN_HAS_BACKUP_PRIVILEGE;
        else if (RtlEqualLuid(&Token->Privileges[i].Luid, &SeRestorePrivilege))
            Flags |= TOKEN_HAS_RESTORE_PRIVILEGE;
        else if (RtlEqualLuid(&Token->Privileges[i].Luid, &SeImpersonatePrivilege))
            Flags |= TOKEN_HAS_IMPERSONATE_PRIVILEGE;
    }

    Token->TokenFlags &= ~(TOKEN_HAS_TRAVERSE_PRIVILEGE |
                           TOKEN_HAS_BACKUP_PRIVILEGE |
                           TOKEN_HAS_RESTORE_PRIVILEGE |
                           TOKEN_HAS_IMPERSONATE_PRIVILEGE);
    Token->TokenFlags |= Flags;
}

/* Takes one privilege away, the array staying whole by moving the last up */
VOID
SepDropTokenPrivilege(
    _Inout_ PTOKEN Token,
    _In_ ULONG Index)
{
    if (Index >= Token->PrivilegeCount) return;

    Token->Privileges[Index] = Token->Privileges[Token->PrivilegeCount - 1];
    Token->PrivilegeCount--;

    SepNoteTokenPrivilegeFlags(Token);
}

/* Takes one group away the same way, the user at the front never moving */
VOID
SepDropTokenGroup(
    _Inout_ PTOKEN Token,
    _In_ ULONG Index)
{
    if (Index == 0 || Index >= Token->UserAndGroupCount) return;

    Token->UserAndGroups[Index] =
        Token->UserAndGroups[Token->UserAndGroupCount - 1];
    Token->UserAndGroupCount--;

    if (Token->DefaultOwnerIndex >= Token->UserAndGroupCount)
        Token->DefaultOwnerIndex = 0;
}

/*
 * Remembers what the token's groups say about it: whether it stands for an
 * administrator, how far it may be trusted, and whether it was cut down.
 */
static
VOID
SepNoteTokenGroupFlags(
    _Inout_ PTOKEN Token)
{
    ULONG i;

    Token->IntegrityLevelIndex = MAXULONG;

    for (i = 0; i < Token->UserAndGroupCount; i++)
    {
        if (Token->UserAndGroups[i].Attributes & SE_GROUP_INTEGRITY)
        {
            Token->IntegrityLevelIndex = i;
            continue;
        }
    }

    if (Token->RestrictedSidCount != 0) Token->TokenFlags |= TOKEN_IS_RESTRICTED;
}

/* A token carries its own lock, since what it says may be changed under it */
static
NTSTATUS
SepMakeTokenLock(
    _Inout_ PTOKEN Token)
{
    Token->TokenLock = ExAllocatePoolWithTag(NonPagedPool,
                                             sizeof(ERESOURCE),
                                             TAG_SE_TOKEN_LOCK);
    if (Token->TokenLock == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    return ExInitializeResourceLite(Token->TokenLock);
}

static
VOID
SepBreakTokenLock(
    _Inout_ PTOKEN Token)
{
    if (Token->TokenLock == NULL) return;

    ExDeleteResourceLite(Token->TokenLock);
    ExFreePoolWithTag(Token->TokenLock, TAG_SE_TOKEN_LOCK);
    Token->TokenLock = NULL;
}

/*
 * Makes a token out of what it is to say. The object owns one block for the
 * parts that are settled once, and a second for the two that may change.
 */
static
NTSTATUS
SepAssembleToken(
    _In_ const SEP_TOKEN_CONTENTS *Contents,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG ChangeableSpare,
    _Out_ PTOKEN *NewToken)
{
    ULONG Fixed;
    PTOKEN Token;
    PUCHAR Room;
    NTSTATUS Status;

    PAGED_CODE();

    *NewToken = NULL;

    if (Contents->UserAndGroupCount == 0) return STATUS_INVALID_PARAMETER;

    Fixed = SepRoomForSids(Contents->UserAndGroupCount, Contents->UserAndGroups) +
            SepRoomForSids(Contents->RestrictedSidCount, Contents->RestrictedSids) +
            Contents->PrivilegeCount * (ULONG)sizeof(LUID_AND_ATTRIBUTES);

    Status = ObCreateObject(AccessMode,
                            SeTokenObjectType,
                            ObjectAttributes,
                            AccessMode,
                            NULL,
                            FIELD_OFFSET(TOKEN, VariablePart) + Fixed,
                            0,
                            0,
                            (PVOID *)&Token);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(Token, FIELD_OFFSET(TOKEN, VariablePart) + Fixed);

    Token->TokenSource = Contents->Source;
    Token->AuthenticationId = Contents->AuthenticationId;
    Token->OriginatingLogonSession = Contents->OriginatingLogonSession;
    Token->ParentTokenId = Contents->ParentTokenId;
    Token->ExpirationTime = Contents->ExpirationTime;
    Token->TokenType = Contents->Type;
    Token->ImpersonationLevel = Contents->Level;
    Token->SessionId = Contents->SessionId;
    Token->MandatoryPolicy = Contents->MandatoryPolicy;
    Token->TokenFlags = Contents->Flags;
    Token->DefaultOwnerIndex = Contents->DefaultOwnerIndex;
    Token->VariableLength = Fixed;

    ExAllocateLocallyUniqueId(&Token->TokenId);
    ExAllocateLocallyUniqueId(&Token->ModifiedId);
    ExAllocateLocallyUniqueId(&Token->ProcUnique);

    Room = (PUCHAR)&Token->VariablePart;

    Token->UserAndGroupCount = Contents->UserAndGroupCount;
    Room = SepWriteSids(&Token->UserAndGroups,
                        Contents->UserAndGroupCount,
                        Contents->UserAndGroups,
                        Room);

    Token->RestrictedSidCount = Contents->RestrictedSidCount;
    Room = SepWriteSids(&Token->RestrictedSids,
                        Contents->RestrictedSidCount,
                        Contents->RestrictedSids,
                        Room);

    Token->PrivilegeCount = Contents->PrivilegeCount;
    if (Contents->PrivilegeCount != 0)
    {
        Token->Privileges = (PLUID_AND_ATTRIBUTES)Room;
        RtlCopyMemory(Token->Privileges,
                      Contents->Privileges,
                      Contents->PrivilegeCount * sizeof(LUID_AND_ATTRIBUTES));
    }

    Status = SepWriteChangeablePart(Token,
                                    Contents->PrimaryGroup,
                                    Contents->DefaultDacl,
                                    ChangeableSpare);
    if (NT_SUCCESS(Status)) Status = SepMakeTokenLock(Token);

    if (NT_SUCCESS(Status))
    {
        SepNoteTokenPrivilegeFlags(Token);
        SepNoteTokenGroupFlags(Token);

        /* A token belongs to the logon it was made for, and holds it open */
        Status = SepRmInsertLogonSessionIntoToken(Token);
    }

    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Token);
        return Status;
    }

    *NewToken = Token;
    return STATUS_SUCCESS;
}

/*
 * Makes a second token that says what the first one says. What was turned off
 * at the time can be dropped for good, which is what a caller asks for when
 * it wants the token as it stands and not as it might become.
 */
static
NTSTATUS
SepCloneToken(
    _In_ PTOKEN Token,
    _In_ BOOLEAN EffectiveOnly,
    _In_ TOKEN_TYPE Type,
    _In_ SECURITY_IMPERSONATION_LEVEL Level,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PTOKEN *NewToken)
{
    SEP_TOKEN_CONTENTS Contents;
    PSID_AND_ATTRIBUTES Groups = NULL;
    PLUID_AND_ATTRIBUTES Privileges = NULL;
    ULONG GroupCount = 0;
    ULONG PrivilegeCount = 0;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    *NewToken = NULL;

    RtlZeroMemory(&Contents, sizeof(Contents));

    SepAcquireTokenLockShared(Token);

    Groups = ExAllocatePoolWithTag(PagedPool,
                                   Token->UserAndGroupCount *
                                       sizeof(SID_AND_ATTRIBUTES),
                                   TAG_SE);
    if (Token->PrivilegeCount != 0)
    {
        Privileges = ExAllocatePoolWithTag(PagedPool,
                                           Token->PrivilegeCount *
                                               sizeof(LUID_AND_ATTRIBUTES),
                                           TAG_SE);
    }

    if (Groups == NULL || (Token->PrivilegeCount != 0 && Privileges == NULL))
    {
        SepReleaseTokenLock(Token);
        if (Groups != NULL) ExFreePoolWithTag(Groups, TAG_SE);
        if (Privileges != NULL) ExFreePoolWithTag(Privileges, TAG_SE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* The user is always kept; a group that is off is kept only if it may
     * still be turned on */
    for (i = 0; i < Token->UserAndGroupCount; i++)
    {
        if (i != 0 && EffectiveOnly &&
            !(Token->UserAndGroups[i].Attributes & SE_GROUP_ENABLED) &&
            !(Token->UserAndGroups[i].Attributes & SE_GROUP_INTEGRITY))
        {
            continue;
        }

        Groups[GroupCount] = Token->UserAndGroups[i];

        if (i == Token->DefaultOwnerIndex) Contents.DefaultOwnerIndex = GroupCount;

        GroupCount++;
    }

    for (i = 0; i < Token->PrivilegeCount; i++)
    {
        if (EffectiveOnly &&
            !(Token->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED))
        {
            continue;
        }

        Privileges[PrivilegeCount++] = Token->Privileges[i];
    }

    Contents.UserAndGroups = Groups;
    Contents.UserAndGroupCount = GroupCount;
    Contents.RestrictedSids = Token->RestrictedSids;
    Contents.RestrictedSidCount = Token->RestrictedSidCount;
    Contents.Privileges = Privileges;
    Contents.PrivilegeCount = PrivilegeCount;
    Contents.PrimaryGroup = Token->PrimaryGroup;
    Contents.DefaultDacl = Token->DefaultDacl;
    Contents.AuthenticationId = Token->AuthenticationId;
    Contents.OriginatingLogonSession = Token->OriginatingLogonSession;
    Contents.ParentTokenId = Token->TokenId;
    Contents.ExpirationTime = Token->ExpirationTime;
    Contents.Source = Token->TokenSource;
    Contents.Type = Type;
    Contents.Level = (Type == TokenImpersonation) ? Level : SecurityAnonymous;
    Contents.SessionId = Token->SessionId;
    Contents.MandatoryPolicy = Token->MandatoryPolicy;
    Contents.Flags = Token->TokenFlags;

    Status = SepAssembleToken(&Contents,
                              ObjectAttributes,
                              AccessMode,
                              0,
                              NewToken);

    SepReleaseTokenLock(Token);

    ExFreePoolWithTag(Groups, TAG_SE);
    if (Privileges != NULL) ExFreePoolWithTag(Privileges, TAG_SE);

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

VOID
SepAcquireTokenLockShared(
    _Inout_ PTOKEN Token)
{
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(Token->TokenLock, TRUE);
}

VOID
SepAcquireTokenLockExclusive(
    _Inout_ PTOKEN Token)
{
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(Token->TokenLock, TRUE);
}

VOID
SepReleaseTokenLock(
    _Inout_ PTOKEN Token)
{
    ExReleaseResourceLite(Token->TokenLock);
    KeLeaveCriticalRegion();
}

/*
 * Lets go of everything a token held: the logon it belonged to, the part of
 * it that could change, and its own lock.
 */
VOID
NTAPI
SepDeleteToken(
    _In_ PVOID ObjectBody)
{
    PTOKEN Token = ObjectBody;

    PAGED_CODE();

    if (Token->LogonSession != NULL) SepRmRemoveLogonSessionFromToken(Token);

    if (Token->DynamicPart != NULL)
    {
        ExFreePoolWithTag(Token->DynamicPart, TAG_SE);
        Token->DynamicPart = NULL;
        Token->PrimaryGroup = NULL;
        Token->DefaultDacl = NULL;
    }

    /* TODO: a token made for a package keeps nothing of its own to let go of */
    SepBreakTokenLock(Token);
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitializeTokenImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Token");

    PAGED_CODE();

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK | OBJ_PERMANENT;
    Initializer.ValidAccessMask = TOKEN_ALL_ACCESS;
    Initializer.GenericMapping = SepTokenMapping;
    Initializer.UseDefaultObject = TRUE;
    Initializer.PoolType = PagedPool;
    Initializer.DefaultPagedPoolCharge = sizeof(TOKEN);
    Initializer.DeleteProcedure = SepDeleteToken;
    Initializer.SecurityProcedure = SeDefaultObjectMethod;

    if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                       &Initializer,
                                       NULL,
                                       &SeTokenObjectType)))
    {
        return FALSE;
    }

    ExAllocateLocallyUniqueId(&SeSystemTokenSource.SourceIdentifier);
    return TRUE;
}

/* How far a token's holder is trusted, as the level its label names */
ULONG
NTAPI
SepGetTokenIntegrityRid(
    _In_ PACCESS_TOKEN _Token)
{
    PTOKEN Token = _Token;
    PISID Label;

    if (Token->IntegrityLevelIndex == MAXULONG)
        return SECURITY_MANDATORY_MEDIUM_RID;

    Label = Token->UserAndGroups[Token->IntegrityLevelIndex].Sid;
    if (Label == NULL || Label->SubAuthorityCount == 0)
        return SECURITY_MANDATORY_MEDIUM_RID;

    return Label->SubAuthority[Label->SubAuthorityCount - 1];
}

/*
 * Makes more room for the two things a token may be told to change, keeping
 * what it already says in the new room.
 */
NTSTATUS
SepGrowChangeablePart(
    _Inout_ PTOKEN Token,
    _In_ ULONG Needed)
{
    PULONG Old = Token->DynamicPart;
    PSID Group = Token->PrimaryGroup;
    PACL Dacl = Token->DefaultDacl;
    NTSTATUS Status;

    PAGED_CODE();

    Status = SepWriteChangeablePart(Token, Group, Dacl, Needed);
    if (!NT_SUCCESS(Status)) return Status;

    if (Old != NULL) ExFreePoolWithTag(Old, TAG_SE);

    return STATUS_SUCCESS;
}

/*
 * The token the system itself runs with: it stands for the system, belongs to
 * the administrators, may do everything, and nothing it holds expires.
 */
CODE_SEG("INIT")
PTOKEN
NTAPI
SepCreateSystemProcessToken(VOID)
{
    static const SEP_SYSTEM_PRIVILEGE Held[] =
    {
        {&SeTcbPrivilege, SEP_PRIVILEGE_ON},
        {&SeChangeNotifyPrivilege, SEP_PRIVILEGE_ON},
        {&SeCreateTokenPrivilege, SEP_PRIVILEGE_ON},
        {&SeAssignPrimaryTokenPrivilege, SEP_PRIVILEGE_ON},
        {&SeLockMemoryPrivilege, SEP_PRIVILEGE_ON},
        {&SeIncreaseQuotaPrivilege, SEP_PRIVILEGE_ON},
        {&SeSecurityPrivilege, SEP_PRIVILEGE_ON},
        {&SeTakeOwnershipPrivilege, SEP_PRIVILEGE_ON},
        {&SeLoadDriverPrivilege, SEP_PRIVILEGE_ON},
        {&SeCreatePagefilePrivilege, SEP_PRIVILEGE_ON},
        {&SeIncreaseBasePriorityPrivilege, SEP_PRIVILEGE_ON},
        {&SeSystemProfilePrivilege, SEP_PRIVILEGE_ON},
        {&SeSystemtimePrivilege, SEP_PRIVILEGE_ON},
        {&SeProfileSingleProcessPrivilege, SEP_PRIVILEGE_ON},
        {&SeCreatePermanentPrivilege, SEP_PRIVILEGE_ON},
        {&SeBackupPrivilege, SEP_PRIVILEGE_ON},
        {&SeRestorePrivilege, SEP_PRIVILEGE_ON},
        {&SeShutdownPrivilege, SEP_PRIVILEGE_ON},
        {&SeDebugPrivilege, SEP_PRIVILEGE_ON},
        {&SeAuditPrivilege, SEP_PRIVILEGE_ON},
        {&SeSystemEnvironmentPrivilege, SEP_PRIVILEGE_ON},
        {&SeRemoteShutdownPrivilege, SEP_PRIVILEGE_ON},
        {&SeUndockPrivilege, SEP_PRIVILEGE_ON},
        {&SeManageVolumePrivilege, SEP_PRIVILEGE_ON},
        {&SeImpersonatePrivilege, SEP_PRIVILEGE_ON},
        {&SeCreateGlobalPrivilege, SEP_PRIVILEGE_ON},
        {&SeIncreaseWorkingSetPrivilege, SEP_PRIVILEGE_ON},
        {&SeTimeZonePrivilege, SEP_PRIVILEGE_ON},
        {&SeCreateSymbolicLinkPrivilege, SEP_PRIVILEGE_ON},
        {&SeRelabelPrivilege, SEP_PRIVILEGE_ON},
        {&SeDelegateSessionUserImpersonatePrivilege, SEP_PRIVILEGE_ON},
    };
    SID_AND_ATTRIBUTES Groups[5];
    LUID_AND_ATTRIBUTES Privileges[RTL_NUMBER_OF(Held)];
    SEP_TOKEN_CONTENTS Contents;
    PTOKEN Token = NULL;
    ULONG i;

    PAGED_CODE();

    Groups[0].Sid = SeLocalSystemSid;
    Groups[0].Attributes = 0;
    Groups[1].Sid = SeAliasAdminsSid;
    Groups[1].Attributes = SEP_GROUP_ON | SE_GROUP_OWNER;
    Groups[2].Sid = SeWorldSid;
    Groups[2].Attributes = SEP_GROUP_ON;
    Groups[3].Sid = SeAuthenticatedUsersSid;
    Groups[3].Attributes = SEP_GROUP_ON;
    Groups[4].Sid = SeSystemMandatorySid;
    Groups[4].Attributes = SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED;

    for (i = 0; i < RTL_NUMBER_OF(Held); i++)
    {
        Privileges[i].Luid = *Held[i].Privilege;
        Privileges[i].Attributes = Held[i].Attributes;
    }

    RtlZeroMemory(&Contents, sizeof(Contents));
    Contents.UserAndGroups = Groups;
    Contents.UserAndGroupCount = RTL_NUMBER_OF(Groups);
    Contents.Privileges = Privileges;
    Contents.PrivilegeCount = RTL_NUMBER_OF(Privileges);
    Contents.PrimaryGroup = SeLocalSystemSid;
    Contents.DefaultDacl = SeSystemDefaultDacl;
    Contents.DefaultOwnerIndex = 1;
    Contents.AuthenticationId = SeSystemAuthenticationId;
    Contents.OriginatingLogonSession = SeSystemAuthenticationId;
    Contents.ExpirationTime.QuadPart = -1;
    Contents.Source = SeSystemTokenSource;
    Contents.Type = TokenPrimary;
    Contents.Level = SecurityAnonymous;
    Contents.MandatoryPolicy = TOKEN_MANDATORY_POLICY_NO_WRITE_UP;

    if (!NT_SUCCESS(SepAssembleToken(&Contents, NULL, KernelMode, 0, &Token)))
        return NULL;

    Token->TokenInUse = TRUE;
    return Token;
}

/*
 * The token anybody who never said who they are is given. One of the two
 * counts the world among its groups and the other does not, because whether
 * an unknown caller is part of the world is the system's to settle.
 */
static
CODE_SEG("INIT")
PTOKEN
SepMakeAnonymousToken(
    _In_ BOOLEAN WithWorld)
{
    SID_AND_ATTRIBUTES Groups[3];
    SEP_TOKEN_CONTENTS Contents;
    PTOKEN Token = NULL;
    ULONG Count = 0;

    PAGED_CODE();

    Groups[Count].Sid = SeAnonymousLogonSid;
    Groups[Count++].Attributes = 0;

    if (WithWorld)
    {
        Groups[Count].Sid = SeWorldSid;
        Groups[Count++].Attributes = SEP_GROUP_ON;
    }

    Groups[Count].Sid = SeUntrustedMandatorySid;
    Groups[Count++].Attributes = SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED;

    RtlZeroMemory(&Contents, sizeof(Contents));
    Contents.UserAndGroups = Groups;
    Contents.UserAndGroupCount = Count;
    Contents.PrimaryGroup = SeAnonymousLogonSid;
    Contents.DefaultDacl = SeSystemAnonymousLogonDacl;
    Contents.AuthenticationId = SeAnonymousAuthenticationId;
    Contents.OriginatingLogonSession = SeAnonymousAuthenticationId;
    Contents.ExpirationTime.QuadPart = -1;
    Contents.Source = SeSystemTokenSource;
    Contents.Type = TokenImpersonation;
    Contents.Level = SecurityImpersonation;
    Contents.MandatoryPolicy = TOKEN_MANDATORY_POLICY_NO_WRITE_UP;

    if (!NT_SUCCESS(SepAssembleToken(&Contents, NULL, KernelMode, 0, &Token)))
        return NULL;

    return Token;
}

CODE_SEG("INIT")
PTOKEN
NTAPI
SepCreateSystemAnonymousLogonToken(VOID)
{
    return SepMakeAnonymousToken(TRUE);
}

CODE_SEG("INIT")
PTOKEN
NTAPI
SepCreateSystemAnonymousLogonTokenNoEveryone(VOID)
{
    return SepMakeAnonymousToken(FALSE);
}

/* MAKING ONE TOKEN FROM ANOTHER ***********************************************/

/* What a caller named to be turned off, matched against what a token holds */
static
BOOLEAN
SepNamedInGroups(
    _In_opt_ PTOKEN_GROUPS Named,
    _In_ PSID Sid)
{
    ULONG i;

    if (Named == NULL) return FALSE;

    for (i = 0; i < Named->GroupCount; i++)
    {
        if (Named->Groups[i].Sid == NULL) continue;
        if (RtlEqualSid(Named->Groups[i].Sid, Sid)) return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
SepNamedInPrivileges(
    _In_opt_ PTOKEN_PRIVILEGES Named,
    _In_ PLUID Privilege)
{
    ULONG i;

    if (Named == NULL) return FALSE;

    for (i = 0; i < Named->PrivilegeCount; i++)
    {
        if (RtlEqualLuid(&Named->Privileges[i].Luid, Privilege)) return TRUE;
    }

    return FALSE;
}

/*
 * Cuts a token down. What is named is turned into a holder that can only
 * count against its holder, what is named among the privileges is dropped,
 * and whoever is named as a restriction is added as one.
 */
NTSTATUS
NTAPI
SeFilterToken(
    _In_ PACCESS_TOKEN ExistingToken,
    _In_ ULONG Flags,
    _In_opt_ PTOKEN_GROUPS SidsToDisable,
    _In_opt_ PTOKEN_PRIVILEGES PrivilegesToDelete,
    _In_opt_ PTOKEN_GROUPS RestrictedSids,
    _Outptr_ PACCESS_TOKEN *FilteredToken)
{
    PTOKEN Token = ExistingToken;
    PSID_AND_ATTRIBUTES Groups = NULL;
    PSID_AND_ATTRIBUTES Restricted = NULL;
    PLUID_AND_ATTRIBUTES Privileges = NULL;
    SEP_TOKEN_CONTENTS Contents;
    ULONG GroupCount = 0;
    ULONG RestrictedCount = 0;
    ULONG PrivilegeCount = 0;
    ULONG Added;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    *FilteredToken = NULL;

    RtlZeroMemory(&Contents, sizeof(Contents));

    SepAcquireTokenLockShared(Token);

    Added = (RestrictedSids != NULL) ? RestrictedSids->GroupCount : 0;

    Groups = ExAllocatePoolWithTag(PagedPool,
                                   Token->UserAndGroupCount *
                                       sizeof(SID_AND_ATTRIBUTES),
                                   TAG_SE);

    if (Token->PrivilegeCount != 0)
    {
        Privileges = ExAllocatePoolWithTag(PagedPool,
                                           Token->PrivilegeCount *
                                               sizeof(LUID_AND_ATTRIBUTES),
                                           TAG_SE);
    }

    if (Token->RestrictedSidCount + Added != 0)
    {
        Restricted = ExAllocatePoolWithTag(PagedPool,
                                           (Token->RestrictedSidCount + Added) *
                                               sizeof(SID_AND_ATTRIBUTES),
                                           TAG_SE);
    }

    if (Groups == NULL ||
        (Token->PrivilegeCount != 0 && Privileges == NULL) ||
        (Token->RestrictedSidCount + Added != 0 && Restricted == NULL))
    {
        SepReleaseTokenLock(Token);

        if (Groups != NULL) ExFreePoolWithTag(Groups, TAG_SE);
        if (Privileges != NULL) ExFreePoolWithTag(Privileges, TAG_SE);
        if (Restricted != NULL) ExFreePoolWithTag(Restricted, TAG_SE);

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (i = 0; i < Token->UserAndGroupCount; i++)
    {
        Groups[GroupCount] = Token->UserAndGroups[i];

        /* A holder that is only counted against cannot be turned back on */
        if (i != 0 && SepNamedInGroups(SidsToDisable, Groups[GroupCount].Sid))
        {
            Groups[GroupCount].Attributes &=
                ~(SE_GROUP_ENABLED | SE_GROUP_ENABLED_BY_DEFAULT);
            Groups[GroupCount].Attributes |= SE_GROUP_USE_FOR_DENY_ONLY;
        }

        if (i == Token->DefaultOwnerIndex) Contents.DefaultOwnerIndex = GroupCount;

        GroupCount++;
    }

    for (i = 0; i < Token->PrivilegeCount; i++)
    {
        if (SepNamedInPrivileges(PrivilegesToDelete, &Token->Privileges[i].Luid))
            continue;

        /* Keeping the most a caller may do means keeping only what nobody
         * has to be trusted for */
        if ((Flags & DISABLE_MAX_PRIVILEGE) &&
            !RtlEqualLuid(&Token->Privileges[i].Luid, &SeChangeNotifyPrivilege))
        {
            continue;
        }

        Privileges[PrivilegeCount++] = Token->Privileges[i];
    }

    for (i = 0; i < Token->RestrictedSidCount; i++)
        Restricted[RestrictedCount++] = Token->RestrictedSids[i];

    for (i = 0; i < Added; i++)
    {
        if (RestrictedSids->Groups[i].Sid == NULL) continue;

        Restricted[RestrictedCount].Sid = RestrictedSids->Groups[i].Sid;
        Restricted[RestrictedCount].Attributes = SE_GROUP_ENABLED;
        RestrictedCount++;
    }

    Contents.UserAndGroups = Groups;
    Contents.UserAndGroupCount = GroupCount;
    Contents.RestrictedSids = Restricted;
    Contents.RestrictedSidCount = RestrictedCount;
    Contents.Privileges = Privileges;
    Contents.PrivilegeCount = PrivilegeCount;
    Contents.PrimaryGroup = Token->PrimaryGroup;
    Contents.DefaultDacl = Token->DefaultDacl;
    Contents.AuthenticationId = Token->AuthenticationId;
    Contents.OriginatingLogonSession = Token->OriginatingLogonSession;
    Contents.ParentTokenId = Token->TokenId;
    Contents.ExpirationTime = Token->ExpirationTime;
    Contents.Source = Token->TokenSource;
    Contents.Type = Token->TokenType;
    Contents.Level = Token->ImpersonationLevel;
    Contents.SessionId = Token->SessionId;
    Contents.MandatoryPolicy = Token->MandatoryPolicy;
    Contents.Flags = Token->TokenFlags | TOKEN_IS_FILTERED;

    if (Flags & SANDBOX_INERT) Contents.Flags |= TOKEN_SANDBOX_INERT;
    if (Flags & WRITE_RESTRICTED) Contents.Flags |= TOKEN_WRITE_RESTRICTED;

    Status = SepAssembleToken(&Contents,
                              NULL,
                              KernelMode,
                              0,
                              (PTOKEN *)FilteredToken);

    SepReleaseTokenLock(Token);

    if (Groups != NULL) ExFreePoolWithTag(Groups, TAG_SE);
    if (Privileges != NULL) ExFreePoolWithTag(Privileges, TAG_SE);
    if (Restricted != NULL) ExFreePoolWithTag(Restricted, TAG_SE);

    return Status;
}

NTSTATUS
NTAPI
SeCopyClientToken(
    _In_ PACCESS_TOKEN Token,
    _In_ SECURITY_IMPERSONATION_LEVEL Level,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PACCESS_TOKEN *NewToken)
{
    return SepCloneToken(Token,
                         FALSE,
                         TokenImpersonation,
                         Level,
                         NULL,
                         PreviousMode,
                         (PTOKEN *)NewToken);
}

/* The token a process started by another one runs with */
NTSTATUS
NTAPI
SeSubProcessToken(
    _In_ PTOKEN Parent,
    _Out_ PTOKEN *Token,
    _In_ BOOLEAN InUse,
    _In_ ULONG SessionId)
{
    PTOKEN New = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    Status = SepCloneToken(Parent,
                           FALSE,
                           TokenPrimary,
                           SecurityAnonymous,
                           NULL,
                           KernelMode,
                           &New);
    if (!NT_SUCCESS(Status)) return Status;

    New->SessionId = SessionId;
    New->TokenInUse = InUse;

    *Token = New;
    return STATUS_SUCCESS;
}

/* SYSTEM CALLS ***************************************************************/

/*
 * Makes a token out of nothing but what the caller says, which is why it is
 * the one call that needs the right to create one at all.
 */
__kernel_entry
NTSTATUS
NTAPI
NtCreateToken(
    _Out_ PHANDLE TokenHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ TOKEN_TYPE TokenType,
    _In_ PLUID AuthenticationId,
    _In_ PLARGE_INTEGER ExpirationTime,
    _In_ PTOKEN_USER TokenUser,
    _In_ PTOKEN_GROUPS TokenGroups,
    _In_ PTOKEN_PRIVILEGES TokenPrivileges,
    _In_opt_ PTOKEN_OWNER TokenOwner,
    _In_ PTOKEN_PRIMARY_GROUP TokenPrimaryGroup,
    _In_opt_ PTOKEN_DEFAULT_DACL TokenDefaultDacl,
    _In_ PTOKEN_SOURCE TokenSource)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PSECURITY_QUALITY_OF_SERVICE CapturedQos = NULL;
    BOOLEAN QosPresent = FALSE;
    PSID_AND_ATTRIBUTES Everyone = NULL;
    PSID_AND_ATTRIBUTES Groups = NULL;
    PLUID_AND_ATTRIBUTES Privileges = NULL;
    SEP_TOKEN_CONTENTS Contents;
    PSID Owner = NULL;
    PSID PrimaryGroup = NULL;
    PACL DefaultDacl = NULL;
    ULONG GroupCount = 0;
    ULONG PrivilegeCount = 0;
    ULONG Length;
    PTOKEN Token = NULL;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG i;

    PAGED_CODE();

    if (!SeSinglePrivilegeCheck(SeCreateTokenPrivilege, PreviousMode))
        return STATUS_PRIVILEGE_NOT_HELD;

    if (TokenType != TokenPrimary && TokenType != TokenImpersonation)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Contents, sizeof(Contents));

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForWriteHandle(TokenHandle);
            ProbeForRead(AuthenticationId, sizeof(LUID), sizeof(ULONG));
            ProbeForRead(ExpirationTime, sizeof(LARGE_INTEGER), sizeof(ULONG));
            ProbeForRead(TokenUser, sizeof(TOKEN_USER), sizeof(ULONG));
            ProbeForRead(TokenGroups, sizeof(TOKEN_GROUPS), sizeof(ULONG));
            ProbeForRead(TokenPrivileges, sizeof(TOKEN_PRIVILEGES), sizeof(ULONG));
            ProbeForRead(TokenPrimaryGroup, sizeof(TOKEN_PRIMARY_GROUP), sizeof(ULONG));
            ProbeForRead(TokenSource, sizeof(TOKEN_SOURCE), sizeof(ULONG));

            if (TokenOwner != NULL)
                ProbeForRead(TokenOwner, sizeof(TOKEN_OWNER), sizeof(ULONG));
            if (TokenDefaultDacl != NULL)
                ProbeForRead(TokenDefaultDacl, sizeof(TOKEN_DEFAULT_DACL), sizeof(ULONG));
        }

        Contents.AuthenticationId = *AuthenticationId;
        Contents.OriginatingLogonSession = *AuthenticationId;
        Contents.ExpirationTime = *ExpirationTime;
        Contents.Source = *TokenSource;
        GroupCount = TokenGroups->GroupCount;
        PrivilegeCount = TokenPrivileges->PrivilegeCount;
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) return Status;

    /* An impersonation token says how far it may be trusted, which the
     * caller names where every other object names its security */
    if (TokenType == TokenImpersonation)
    {
        Status = SepCaptureSecurityQualityOfService(ObjectAttributes,
                                                    PreviousMode,
                                                    PagedPool,
                                                    FALSE,
                                                    &CapturedQos,
                                                    &QosPresent);
        if (!NT_SUCCESS(Status)) return Status;

        Contents.Level = (QosPresent && CapturedQos != NULL)
                             ? CapturedQos->ImpersonationLevel
                             : SecurityImpersonation;
    }

    Status = SeCaptureSidAndAttributesArray(&TokenUser->User,
                                            1,
                                            PreviousMode,
                                            NULL,
                                            0,
                                            PagedPool,
                                            TRUE,
                                            &Everyone,
                                            &Length);
    if (NT_SUCCESS(Status) && GroupCount != 0)
    {
        Status = SeCaptureSidAndAttributesArray(TokenGroups->Groups,
                                                GroupCount,
                                                PreviousMode,
                                                NULL,
                                                0,
                                                PagedPool,
                                                TRUE,
                                                &Groups,
                                                &Length);
    }

    if (NT_SUCCESS(Status) && PrivilegeCount != 0)
    {
        Length = 0;
        Status = SeCaptureLuidAndAttributesArray(TokenPrivileges->Privileges,
                                                 PrivilegeCount,
                                                 PreviousMode,
                                                 NULL,
                                                 0,
                                                 PagedPool,
                                                 TRUE,
                                                 &Privileges,
                                                 &Length);
    }

    if (NT_SUCCESS(Status) && TokenOwner != NULL)
    {
        Status = SepCaptureSid(TokenOwner->Owner,
                               PreviousMode,
                               PagedPool,
                               TRUE,
                               &Owner);
    }

    if (NT_SUCCESS(Status))
    {
        Status = SepCaptureSid(TokenPrimaryGroup->PrimaryGroup,
                               PreviousMode,
                               PagedPool,
                               TRUE,
                               &PrimaryGroup);
    }

    if (NT_SUCCESS(Status) && TokenDefaultDacl != NULL &&
        TokenDefaultDacl->DefaultDacl != NULL)
    {
        Status = SepCaptureAcl(TokenDefaultDacl->DefaultDacl,
                               PreviousMode,
                               PagedPool,
                               TRUE,
                               &DefaultDacl);
    }

    if (NT_SUCCESS(Status))
    {
        PSID_AND_ATTRIBUTES All;

        /* The user comes first and the groups follow, so that whoever reads
         * the token finds its holder without being told where to look */
        All = ExAllocatePoolWithTag(PagedPool,
                                    (GroupCount + 1) * sizeof(SID_AND_ATTRIBUTES),
                                    TAG_SE);
        if (All == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            All[0] = Everyone[0];
            All[0].Attributes = 0;

            for (i = 0; i < GroupCount; i++) All[i + 1] = Groups[i];

            Contents.UserAndGroups = All;
            Contents.UserAndGroupCount = GroupCount + 1;
            Contents.Privileges = Privileges;
            Contents.PrivilegeCount = PrivilegeCount;
            Contents.PrimaryGroup = PrimaryGroup;
            Contents.DefaultDacl = DefaultDacl;
            Contents.Type = TokenType;
            Contents.MandatoryPolicy = TOKEN_MANDATORY_POLICY_NO_WRITE_UP;

            /* Whoever was named to own what this token makes has to be
             * somebody the token speaks for */
            if (Owner != NULL)
            {
                Contents.DefaultOwnerIndex = MAXULONG;

                for (i = 0; i < Contents.UserAndGroupCount; i++)
                {
                    if (All[i].Sid == NULL) continue;
                    if (!RtlEqualSid(All[i].Sid, Owner)) continue;

                    Contents.DefaultOwnerIndex = i;
                    break;
                }

                if (Contents.DefaultOwnerIndex == MAXULONG)
                    Status = STATUS_INVALID_OWNER;
            }

            if (NT_SUCCESS(Status))
            {
                Status = SepAssembleToken(&Contents,
                                          ObjectAttributes,
                                          PreviousMode,
                                          0,
                                          &Token);
            }

            ExFreePoolWithTag(All, TAG_SE);
        }
    }

    if (NT_SUCCESS(Status))
    {
        Status = ObInsertObject(Token,
                                NULL,
                                DesiredAccess,
                                0,
                                NULL,
                                &Handle);
        if (NT_SUCCESS(Status))
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

            if (!NT_SUCCESS(Status)) ObCloseHandle(Handle, PreviousMode);
        }
    }

    if (DefaultDacl != NULL) SepReleaseAcl(DefaultDacl, PreviousMode, TRUE);
    if (PrimaryGroup != NULL) SepReleaseSid(PrimaryGroup, PreviousMode, TRUE);
    if (Owner != NULL) SepReleaseSid(Owner, PreviousMode, TRUE);
    if (Privileges != NULL)
        SeReleaseLuidAndAttributesArray(Privileges, PreviousMode, TRUE);
    if (Groups != NULL)
        SeReleaseSidAndAttributesArray(Groups, PreviousMode, TRUE);
    if (Everyone != NULL)
        SeReleaseSidAndAttributesArray(Everyone, PreviousMode, TRUE);
    if (CapturedQos != NULL)
        SepReleaseSecurityQualityOfService(CapturedQos, PreviousMode, FALSE);

    return Status;
}

/*
 * Makes a second token saying what the first says. A token taken from
 * somebody else cannot be handed on further than it was given.
 */
__kernel_entry
NTSTATUS
NTAPI
NtDuplicateToken(
    _In_ HANDLE ExistingTokenHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ BOOLEAN EffectiveOnly,
    _In_ TOKEN_TYPE TokenType,
    _Out_ PHANDLE NewTokenHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PSECURITY_QUALITY_OF_SERVICE Qos = NULL;
    SECURITY_IMPERSONATION_LEVEL Level = SecurityImpersonation;
    PTOKEN Token = NULL;
    PTOKEN New = NULL;
    BOOLEAN QosPresent = FALSE;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (TokenType != TokenPrimary && TokenType != TokenImpersonation)
        return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(NewTokenHandle);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = SepCaptureSecurityQualityOfService(ObjectAttributes,
                                                PreviousMode,
                                                PagedPool,
                                                FALSE,
                                                &Qos,
                                                &QosPresent);
    if (!NT_SUCCESS(Status)) return Status;

    if (QosPresent && Qos != NULL) Level = Qos->ImpersonationLevel;

    Status = ObReferenceObjectByHandle(ExistingTokenHandle,
                                       TOKEN_DUPLICATE,
                                       SeTokenObjectType,
                                       PreviousMode,
                                       (PVOID *)&Token,
                                       NULL);
    if (NT_SUCCESS(Status))
    {
        if (Token->TokenType == TokenImpersonation &&
            TokenType == TokenImpersonation &&
            Level > Token->ImpersonationLevel)
        {
            Status = STATUS_BAD_IMPERSONATION_LEVEL;
        }
        else
        {
            Status = SepCloneToken(Token,
                                   EffectiveOnly,
                                   TokenType,
                                   Level,
                                   ObjectAttributes,
                                   PreviousMode,
                                   &New);
        }

        ObDereferenceObject(Token);
    }

    if (NT_SUCCESS(Status))
    {
        Status = ObInsertObject(New, NULL, DesiredAccess, 0, NULL, &Handle);
        if (NT_SUCCESS(Status))
        {
            _SEH2_TRY
            {
                *NewTokenHandle = Handle;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(Status)) ObCloseHandle(Handle, PreviousMode);
        }
    }

    if (Qos != NULL)
        SepReleaseSecurityQualityOfService(Qos, PreviousMode, FALSE);

    return Status;
}

__kernel_entry
NTSTATUS
NTAPI
NtFilterToken(
    _In_ HANDLE ExistingTokenHandle,
    _In_ ULONG Flags,
    _In_opt_ PTOKEN_GROUPS SidsToDisable,
    _In_opt_ PTOKEN_PRIVILEGES PrivilegesToDelete,
    _In_opt_ PTOKEN_GROUPS RestrictedSids,
    _Out_ PHANDLE NewTokenHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PTOKEN Token = NULL;
    PTOKEN New = NULL;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(NewTokenHandle);

            if (SidsToDisable != NULL)
                ProbeForRead(SidsToDisable, sizeof(TOKEN_GROUPS), sizeof(ULONG));
            if (PrivilegesToDelete != NULL)
                ProbeForRead(PrivilegesToDelete, sizeof(TOKEN_PRIVILEGES), sizeof(ULONG));
            if (RestrictedSids != NULL)
                ProbeForRead(RestrictedSids, sizeof(TOKEN_GROUPS), sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(ExistingTokenHandle,
                                       TOKEN_DUPLICATE,
                                       SeTokenObjectType,
                                       PreviousMode,
                                       (PVOID *)&Token,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    /* TODO: what the caller named is read where it stands, not copied first */
    Status = SeFilterToken(Token,
                           Flags,
                           SidsToDisable,
                           PrivilegesToDelete,
                           RestrictedSids,
                           (PACCESS_TOKEN *)&New);

    ObDereferenceObject(Token);

    if (!NT_SUCCESS(Status)) return Status;

    Status = ObInsertObject(New, NULL, TOKEN_ALL_ACCESS, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    _SEH2_TRY
    {
        *NewTokenHandle = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status)) ObCloseHandle(Handle, PreviousMode);

    return Status;
}

/* A token that stands for one package and nothing else */
__kernel_entry
NTSTATUS
NTAPI
NtCreateLowBoxToken(
    _Out_ PHANDLE TokenHandle,
    _In_ HANDLE ExistingTokenHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ PSID PackageSid,
    _In_ ULONG CapabilityCount,
    _In_reads_opt_(CapabilityCount) PSID_AND_ATTRIBUTES Capabilities,
    _In_ ULONG HandleCount,
    _In_reads_opt_(HandleCount) HANDLE *Handles)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PSEP_LOWBOX_INFO Box = NULL;
    PSID Package = NULL;
    PSID_AND_ATTRIBUTES Caps = NULL;
    PTOKEN Token = NULL;
    PTOKEN New = NULL;
    HANDLE Handle;
    NTSTATUS Status;
    ULONG Length;

    PAGED_CODE();

    if (PackageSid == NULL) return STATUS_INVALID_PARAMETER;

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

    Status = SepCaptureSid(PackageSid, PreviousMode, PagedPool, TRUE, &Package);
    if (!NT_SUCCESS(Status)) return Status;

    if (CapabilityCount != 0)
    {
        Status = SeCaptureSidAndAttributesArray(Capabilities,
                                                CapabilityCount,
                                                PreviousMode,
                                                NULL,
                                                0,
                                                PagedPool,
                                                TRUE,
                                                &Caps,
                                                &Length);
    }

    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(ExistingTokenHandle,
                                           TOKEN_DUPLICATE,
                                           SeTokenObjectType,
                                           PreviousMode,
                                           (PVOID *)&Token,
                                           NULL);
    }

    if (NT_SUCCESS(Status))
    {
        Status = SepCloneToken(Token,
                               FALSE,
                               Token->TokenType,
                               Token->ImpersonationLevel,
                               ObjectAttributes,
                               PreviousMode,
                               &New);
        ObDereferenceObject(Token);
    }

    if (NT_SUCCESS(Status))
    {
        Box = ExAllocatePoolWithTag(PagedPool, sizeof(*Box), TAG_SE);
        if (Box == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            RtlZeroMemory(Box, sizeof(*Box));
            Box->PackageSid = Package;
            Box->CapabilityCount = CapabilityCount;
            Box->Capabilities = Caps;

            /* TODO: the handles a package is given are not kept */
            New->LowBoxInfo = Box;
            New->TokenFlags |= TOKEN_LOWBOX;

            Package = NULL;
            Caps = NULL;
        }
    }

    if (NT_SUCCESS(Status))
    {
        Status = ObInsertObject(New, NULL, DesiredAccess, 0, NULL, &Handle);
        if (NT_SUCCESS(Status))
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

            if (!NT_SUCCESS(Status)) ObCloseHandle(Handle, PreviousMode);
        }
    }
    else if (New != NULL)
    {
        ObDereferenceObject(New);
    }

    UNREFERENCED_PARAMETER(HandleCount);
    UNREFERENCED_PARAMETER(Handles);

    if (Caps != NULL) SeReleaseSidAndAttributesArray(Caps, PreviousMode, TRUE);
    if (Package != NULL) SepReleaseSid(Package, PreviousMode, TRUE);

    return Status;
}
