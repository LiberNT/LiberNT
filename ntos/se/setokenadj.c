/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Adjusting the privileges and groups of a token
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE TYPES **************************************************************/

/* One change a request asks of one entry of the token */
typedef struct _SEP_TOKEN_CHANGE
{
    ULONG Index;
    ULONG Attributes;
    BOOLEAN Remove;
} SEP_TOKEN_CHANGE, *PSEP_TOKEN_CHANGE;

/* PRIVATE FUNCTIONS **********************************************************/

static
ULONG
SepPrivilegeStateLength(
    _In_ ULONG Count)
{
    return (ULONG)(FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) +
                   (SIZE_T)Count * sizeof(LUID_AND_ATTRIBUTES));
}

/*
 * Takes the privileges a caller asks to change out of its memory, so the
 * token is never adjusted from a buffer the caller can still rewrite.
 */
static
NTSTATUS
SepCaptureRequestedPrivileges(
    _In_ PTOKEN_PRIVILEGES NewState,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PULONG Count,
    _Outptr_result_maybenull_ PLUID_AND_ATTRIBUTES *Captured)
{
    PLUID_AND_ATTRIBUTES Array;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Entries = 0;
    SIZE_T Size;

    PAGED_CODE();

    *Count = 0;
    *Captured = NULL;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(NewState,
                         FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges),
                         TYPE_ALIGNMENT(TOKEN_PRIVILEGES));
            Entries = NewState->PrivilegeCount;
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
        Entries = NewState->PrivilegeCount;
    }

    if (Entries == 0) return STATUS_SUCCESS;

    if (Entries > MAXULONG / sizeof(LUID_AND_ATTRIBUTES))
        return STATUS_INVALID_PARAMETER;

    Size = (SIZE_T)Entries * sizeof(LUID_AND_ATTRIBUTES);

    Array = ExAllocatePoolWithTag(PagedPool, Size, TAG_SE);
    if (Array == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
        {
            ProbeForRead(NewState->Privileges,
                         Size,
                         TYPE_ALIGNMENT(LUID_AND_ATTRIBUTES));
        }

        RtlCopyMemory(Array, NewState->Privileges, Size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Array, TAG_SE);
        return Status;
    }

    *Count = Entries;
    *Captured = Array;

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * Enables, disables or removes privileges of a token. What was there before is
 * reported first, so a caller given too small a buffer finds the token
 * untouched and can ask again with the size it was told.
 */
NTSTATUS
NTAPI
NtAdjustPrivilegesToken(
    _In_ HANDLE TokenHandle,
    _In_ BOOLEAN DisableAllPrivileges,
    _In_opt_ PTOKEN_PRIVILEGES NewState,
    _In_ ULONG BufferLength,
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PTOKEN_PRIVILEGES PreviousState,
    _Out_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ACCESS_MASK Required = TOKEN_ADJUST_PRIVILEGES;
    PLUID_AND_ATTRIBUTES Request = NULL;
    PSEP_TOKEN_CHANGE Changes = NULL;
    BOOLEAN AllAssigned = TRUE;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG RequestCount = 0;
    ULONG ChangeCount = 0;
    ULONG MaxChanges;
    ULONG Needed;
    ULONG Index;
    ULONG Entry;
    PTOKEN Token;

    PAGED_CODE();

    /* Without a list and without being told to disable all, there is nothing
     * to act on */
    if (!DisableAllPrivileges && NewState == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Reading back what was there is a read of the token */
    if (PreviousState != NULL) Required |= TOKEN_QUERY;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (PreviousState != NULL)
            {
                ProbeForWrite(PreviousState,
                              BufferLength,
                              TYPE_ALIGNMENT(TOKEN_PRIVILEGES));
            }

            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = ObReferenceObjectByHandle(TokenHandle,
                                       Required,
                                       SeTokenObjectType,
                                       PreviousMode,
                                       (PVOID *)&Token,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (!DisableAllPrivileges)
    {
        Status = SepCaptureRequestedPrivileges(NewState,
                                               PreviousMode,
                                               &RequestCount,
                                               &Request);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(Token);
            return Status;
        }
    }

    SepAcquireTokenLockExclusive(Token);

    MaxChanges = DisableAllPrivileges ? Token->PrivilegeCount : RequestCount;
    if (MaxChanges != 0)
    {
        Changes = ExAllocatePoolWithTag(PagedPool,
                                        (SIZE_T)MaxChanges * sizeof(SEP_TOKEN_CHANGE),
                                        TAG_SE);
        if (Changes == NULL)
        {
            SepReleaseTokenLock(Token);
            if (Request != NULL) ExFreePoolWithTag(Request, TAG_SE);
            ObDereferenceObject(Token);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (DisableAllPrivileges)
    {
        for (Index = 0; Index < Token->PrivilegeCount; Index++)
        {
            if (!(Token->Privileges[Index].Attributes & SE_PRIVILEGE_ENABLED))
                continue;

            Changes[ChangeCount].Index = Index;
            Changes[ChangeCount].Attributes =
                Token->Privileges[Index].Attributes & ~SE_PRIVILEGE_ENABLED;
            Changes[ChangeCount].Remove = FALSE;
            ChangeCount++;
        }
    }
    else
    {
        for (Entry = 0; Entry < RequestCount; Entry++)
        {
            BOOLEAN Found = FALSE;

            for (Index = 0; Index < Token->PrivilegeCount; Index++)
            {
                if (!RtlEqualLuid(&Token->Privileges[Index].Luid,
                                  &Request[Entry].Luid))
                {
                    continue;
                }

                Found = TRUE;
                Changes[ChangeCount].Index = Index;
                Changes[ChangeCount].Attributes =
                    Token->Privileges[Index].Attributes;

                if (Request[Entry].Attributes & SE_PRIVILEGE_REMOVED)
                {
                    Changes[ChangeCount].Remove = TRUE;
                }
                else
                {
                    Changes[ChangeCount].Remove = FALSE;

                    if (Request[Entry].Attributes & SE_PRIVILEGE_ENABLED)
                        Changes[ChangeCount].Attributes |= SE_PRIVILEGE_ENABLED;
                    else
                        Changes[ChangeCount].Attributes &= ~SE_PRIVILEGE_ENABLED;
                }

                ChangeCount++;
                break;
            }

            /* A privilege the token does not hold cannot be assigned */
            if (!Found) AllAssigned = FALSE;
        }
    }

    Needed = SepPrivilegeStateLength(ChangeCount);

    /* Too small an answer buffer leaves the token as it was */
    if (PreviousState != NULL && BufferLength < Needed)
    {
        SepReleaseTokenLock(Token);

        if (Changes != NULL) ExFreePoolWithTag(Changes, TAG_SE);
        if (Request != NULL) ExFreePoolWithTag(Request, TAG_SE);
        ObDereferenceObject(Token);

        if (ReturnLength != NULL)
        {
            _SEH2_TRY
            {
                *ReturnLength = Needed;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            _SEH2_END;
        }

        return STATUS_BUFFER_TOO_SMALL;
    }

    if (PreviousState != NULL)
    {
        _SEH2_TRY
        {
            PreviousState->PrivilegeCount = ChangeCount;

            for (Index = 0; Index < ChangeCount; Index++)
            {
                PreviousState->Privileges[Index] =
                    Token->Privileges[Changes[Index].Index];
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            SepReleaseTokenLock(Token);
            if (Changes != NULL) ExFreePoolWithTag(Changes, TAG_SE);
            if (Request != NULL) ExFreePoolWithTag(Request, TAG_SE);
            ObDereferenceObject(Token);
            return Status;
        }
    }

    for (Index = 0; Index < ChangeCount; Index++)
    {
        if (Changes[Index].Remove) continue;

        Token->Privileges[Changes[Index].Index].Attributes =
            Changes[Index].Attributes;
    }

    /*
     * Removals go last and from the end, because taking one out closes the
     * gap and would move the entries the remaining changes name.
     */
    for (Index = Token->PrivilegeCount; Index > 0; Index--)
    {
        for (Entry = 0; Entry < ChangeCount; Entry++)
        {
            if (!Changes[Entry].Remove || Changes[Entry].Index != Index - 1)
                continue;

            SepDropTokenPrivilege(Token, Index - 1);
            break;
        }
    }

    if (ChangeCount != 0)
    {
        SepNoteTokenPrivilegeFlags(Token);

        /* Anything that captured this token's identity has to see it changed */
        ExAllocateLocallyUniqueId(&Token->ModifiedId);
    }

    SepReleaseTokenLock(Token);

    if (Changes != NULL) ExFreePoolWithTag(Changes, TAG_SE);
    if (Request != NULL) ExFreePoolWithTag(Request, TAG_SE);
    ObDereferenceObject(Token);

    if (ReturnLength != NULL)
    {
        _SEH2_TRY
        {
            *ReturnLength = Needed;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }

    return AllAssigned ? STATUS_SUCCESS : STATUS_NOT_ALL_ASSIGNED;
}

/*
 * Enables or disables groups of a token, or puts them all back to the state
 * they were created with. The token's user is its first entry and is not a
 * group, so it is never touched.
 */
NTSTATUS
NTAPI
NtAdjustGroupsToken(
    _In_ HANDLE TokenHandle,
    _In_ BOOLEAN ResetToDefault,
    _In_opt_ PTOKEN_GROUPS NewState,
    _In_opt_ ULONG BufferLength,
    _Out_writes_bytes_to_opt_(BufferLength, *ReturnLength) PTOKEN_GROUPS PreviousState,
    _When_(PreviousState != NULL, _Out_) PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    ACCESS_MASK Required = TOKEN_ADJUST_GROUPS;
    PSID_AND_ATTRIBUTES Request = NULL;
    PSEP_TOKEN_CHANGE Changes = NULL;
    BOOLEAN AllAssigned = TRUE;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG CapturedLength = 0;
    ULONG RequestCount = 0;
    ULONG ChangeCount = 0;
    ULONG SidsLength = 0;
    ULONG MaxChanges;
    ULONG Needed;
    ULONG Index;
    ULONG Entry;
    PTOKEN Token;
    PUCHAR Cursor;

    PAGED_CODE();

    if (!ResetToDefault && NewState == NULL) return STATUS_INVALID_PARAMETER;

    if (PreviousState != NULL) Required |= TOKEN_QUERY;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (PreviousState != NULL)
            {
                ProbeForWrite(PreviousState,
                              BufferLength,
                              TYPE_ALIGNMENT(TOKEN_GROUPS));
            }

            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);

            if (!ResetToDefault)
            {
                ProbeForRead(NewState,
                             FIELD_OFFSET(TOKEN_GROUPS, Groups),
                             TYPE_ALIGNMENT(TOKEN_GROUPS));
                RequestCount = NewState->GroupCount;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status)) return Status;
    }
    else if (!ResetToDefault)
    {
        RequestCount = NewState->GroupCount;
    }

    Status = ObReferenceObjectByHandle(TokenHandle,
                                       Required,
                                       SeTokenObjectType,
                                       PreviousMode,
                                       (PVOID *)&Token,
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;

    if (!ResetToDefault && RequestCount != 0)
    {
        /* The identifiers arrive as pointers into the caller's memory, so the
         * whole array is taken out of it before the token is touched */
        Status = SeCaptureSidAndAttributesArray(NewState->Groups,
                                                RequestCount,
                                                PreviousMode,
                                                NULL,
                                                0,
                                                PagedPool,
                                                TRUE,
                                                &Request,
                                                &CapturedLength);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(Token);
            return Status;
        }
    }

    SepAcquireTokenLockExclusive(Token);

    /* Entry zero is the user, so at most everything after it can change */
    MaxChanges = ResetToDefault ? Token->UserAndGroupCount : RequestCount;
    if (MaxChanges != 0)
    {
        Changes = ExAllocatePoolWithTag(PagedPool,
                                        (SIZE_T)MaxChanges * sizeof(SEP_TOKEN_CHANGE),
                                        TAG_SE);
        if (Changes == NULL)
        {
            SepReleaseTokenLock(Token);
            if (Request != NULL)
                SeReleaseSidAndAttributesArray(Request, PreviousMode, TRUE);
            ObDereferenceObject(Token);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (ResetToDefault)
    {
        for (Index = 1; Index < Token->UserAndGroupCount; Index++)
        {
            ULONG Attributes = Token->UserAndGroups[Index].Attributes;
            ULONG Wanted = Attributes & ~SE_GROUP_ENABLED;

            if (Attributes & SE_GROUP_ENABLED_BY_DEFAULT)
                Wanted |= SE_GROUP_ENABLED;

            if (Wanted == Attributes) continue;

            Changes[ChangeCount].Index = Index;
            Changes[ChangeCount].Attributes = Wanted;
            Changes[ChangeCount].Remove = FALSE;
            ChangeCount++;
        }
    }
    else
    {
        for (Entry = 0; Entry < RequestCount; Entry++)
        {
            BOOLEAN Found = FALSE;

            for (Index = 1; Index < Token->UserAndGroupCount; Index++)
            {
                ULONG Attributes = Token->UserAndGroups[Index].Attributes;

                if (!RtlEqualSid(Token->UserAndGroups[Index].Sid,
                                 Request[Entry].Sid))
                {
                    continue;
                }

                Found = TRUE;

                /* A group the token must always carry cannot be turned off */
                if ((Attributes & SE_GROUP_MANDATORY) &&
                    !(Request[Entry].Attributes & SE_GROUP_ENABLED))
                {
                    SepReleaseTokenLock(Token);
                    ExFreePoolWithTag(Changes, TAG_SE);
                    SeReleaseSidAndAttributesArray(Request, PreviousMode, TRUE);
                    ObDereferenceObject(Token);
                    return STATUS_CANT_DISABLE_MANDATORY;
                }

                Changes[ChangeCount].Index = Index;
                Changes[ChangeCount].Remove = FALSE;

                if (Request[Entry].Attributes & SE_GROUP_ENABLED)
                    Changes[ChangeCount].Attributes = Attributes | SE_GROUP_ENABLED;
                else
                    Changes[ChangeCount].Attributes = Attributes & ~SE_GROUP_ENABLED;

                ChangeCount++;
                break;
            }

            /* TODO: a group the token does not carry is reported the way an
             * unheld privilege is, which Windows 11 has not been asked */
            if (!Found) AllAssigned = FALSE;
        }
    }

    /* The answer carries the identifiers themselves, not pointers into the
     * token, so it needs room for them after the array */
    for (Index = 0; Index < ChangeCount; Index++)
    {
        SidsLength += (ULONG)RtlLengthSid(
            Token->UserAndGroups[Changes[Index].Index].Sid);
    }

    Needed = (ULONG)(FIELD_OFFSET(TOKEN_GROUPS, Groups) +
                     (SIZE_T)ChangeCount * sizeof(SID_AND_ATTRIBUTES) +
                     SidsLength);

    if (PreviousState != NULL && BufferLength < Needed)
    {
        SepReleaseTokenLock(Token);

        if (Changes != NULL) ExFreePoolWithTag(Changes, TAG_SE);
        if (Request != NULL)
            SeReleaseSidAndAttributesArray(Request, PreviousMode, TRUE);
        ObDereferenceObject(Token);

        if (ReturnLength != NULL)
        {
            _SEH2_TRY
            {
                *ReturnLength = Needed;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            _SEH2_END;
        }

        return STATUS_BUFFER_TOO_SMALL;
    }

    if (PreviousState != NULL)
    {
        _SEH2_TRY
        {
            PreviousState->GroupCount = ChangeCount;

            Cursor = (PUCHAR)PreviousState +
                     FIELD_OFFSET(TOKEN_GROUPS, Groups) +
                     (SIZE_T)ChangeCount * sizeof(SID_AND_ATTRIBUTES);

            for (Index = 0; Index < ChangeCount; Index++)
            {
                PSID_AND_ATTRIBUTES Group =
                    &Token->UserAndGroups[Changes[Index].Index];
                ULONG Length = (ULONG)RtlLengthSid(Group->Sid);

                RtlCopyMemory(Cursor, Group->Sid, Length);

                PreviousState->Groups[Index].Sid = (PSID)Cursor;
                PreviousState->Groups[Index].Attributes = Group->Attributes;

                Cursor += Length;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            SepReleaseTokenLock(Token);
            if (Changes != NULL) ExFreePoolWithTag(Changes, TAG_SE);
            if (Request != NULL)
                SeReleaseSidAndAttributesArray(Request, PreviousMode, TRUE);
            ObDereferenceObject(Token);
            return Status;
        }
    }

    for (Index = 0; Index < ChangeCount; Index++)
    {
        Token->UserAndGroups[Changes[Index].Index].Attributes =
            Changes[Index].Attributes;
    }

    if (ChangeCount != 0)
    {
        /* Which groups are on decides what the token may traverse */
        SepNoteTokenPrivilegeFlags(Token);
        ExAllocateLocallyUniqueId(&Token->ModifiedId);
    }

    SepReleaseTokenLock(Token);

    if (Changes != NULL) ExFreePoolWithTag(Changes, TAG_SE);
    if (Request != NULL)
        SeReleaseSidAndAttributesArray(Request, PreviousMode, TRUE);
    ObDereferenceObject(Token);

    if (ReturnLength != NULL)
    {
        _SEH2_TRY
        {
            *ReturnLength = Needed;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        _SEH2_END;
    }

    return AllAssigned ? STATUS_SUCCESS : STATUS_NOT_ALL_ASSIGNED;
}

/* EOF */
