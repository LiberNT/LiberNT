/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Object type lists for a typed access check
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * An object ACE names the type it applies to only when it says it carries one;
 * the allowed and denied forms hold that name in the same place.
 */
PGUID
SepGetObjectTypeGuidFromAce(
    _In_ PACE Ace,
    _In_ BOOLEAN IsAceDenied)
{
    if (IsAceDenied)
    {
        PACCESS_DENIED_OBJECT_ACE Denied = (PACCESS_DENIED_OBJECT_ACE)Ace;

        if (!(Denied->Flags & ACE_OBJECT_TYPE_PRESENT)) return NULL;

        return &Denied->ObjectType;
    }
    else
    {
        PACCESS_ALLOWED_OBJECT_ACE Allowed = (PACCESS_ALLOWED_OBJECT_ACE)Ace;

        if (!(Allowed->Flags & ACE_OBJECT_TYPE_PRESENT)) return NULL;

        return &Allowed->ObjectType;
    }
}

BOOLEAN
SepObjectTypeGuidInList(
    _In_reads_(ObjectTypeListLength) POBJECT_TYPE_LIST_INTERNAL ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ PGUID ObjectTypeGuid,
    _Out_ PULONG ObjectIndex)
{
    ULONG Index;

    *ObjectIndex = 0;

    for (Index = 0; Index < ObjectTypeListLength; Index++)
    {
        if (RtlCompareMemory(&ObjectTypeList[Index].ObjectTypeGuid,
                             ObjectTypeGuid,
                             sizeof(GUID)) == sizeof(GUID))
        {
            *ObjectIndex = Index;
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * The list a caller hands in describes a hierarchy flattened in order: it
 * starts at the object itself and each child sits one level below its
 * parent. It
 * is taken into a form of our own, so what comes back is always allocated.
 */
NTSTATUS
SeCaptureObjectTypeList(
    _In_reads_opt_(ObjectTypeListLength) POBJECT_TYPE_LIST ObjectTypeList,
    _In_ ULONG ObjectTypeListLength,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ POBJECT_TYPE_LIST_INTERNAL *CapturedObjectTypeList)
{
    POBJECT_TYPE_LIST_INTERNAL Captured;
    NTSTATUS Status = STATUS_SUCCESS;
    USHORT Previous = 0;
    SIZE_T Size;
    ULONG Index;

    PAGED_CODE();

    *CapturedObjectTypeList = NULL;

    if (ObjectTypeList == NULL || ObjectTypeListLength == 0)
        return STATUS_SUCCESS;

    /* A length that cannot be sized is not a list */
    if (ObjectTypeListLength > MAXULONG / sizeof(OBJECT_TYPE_LIST_INTERNAL))
        return STATUS_INVALID_PARAMETER;

    Size = (SIZE_T)ObjectTypeListLength * sizeof(OBJECT_TYPE_LIST_INTERNAL);

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(ObjectTypeList,
                         (SIZE_T)ObjectTypeListLength * sizeof(OBJECT_TYPE_LIST),
                         TYPE_ALIGNMENT(OBJECT_TYPE_LIST));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status)) return Status;
    }

    Captured = ExAllocatePoolWithTag(PagedPool, Size, TAG_SE);
    if (Captured == NULL) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Captured, Size);

    _SEH2_TRY
    {
        for (Index = 0; Index < ObjectTypeListLength; Index++)
        {
            USHORT Level = ObjectTypeList[Index].Level;
            PGUID Guid = ObjectTypeList[Index].ObjectType;

            /*
             * The object itself is the one entry at the root level: anything
             * after it sits below something, and no level may be skipped.
             */
            if (Index == 0 ? (Level != ACCESS_OBJECT_GUID)
                           : (Level == ACCESS_OBJECT_GUID ||
                              Level > Previous + 1))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (Guid == NULL)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (PreviousMode != KernelMode)
                ProbeForRead(Guid, sizeof(GUID), TYPE_ALIGNMENT(GUID));

            Captured[Index].ObjectTypeGuid = *Guid;
            Captured[Index].Level = Level;
            Previous = Level;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Captured, TAG_SE);
        return Status;
    }

    *CapturedObjectTypeList = Captured;

    return STATUS_SUCCESS;
}

VOID
SeReleaseObjectTypeList(
    _In_ _Post_invalid_ POBJECT_TYPE_LIST_INTERNAL CapturedObjectTypeList,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PAGED_CODE();

    /* The capture converts the list whatever the mode asked for, so there is
     * always something of our own to give back */
    UNREFERENCED_PARAMETER(PreviousMode);

    if (CapturedObjectTypeList != NULL)
        ExFreePoolWithTag(CapturedObjectTypeList, TAG_SE);
}

/* EOF */
