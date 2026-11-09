/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Waiting on objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/unaligned.h>
#define NDEBUG
#include <debug.h>

/* WAIT OBJECT RESOLUTION *****************************************************/

PVOID
ObpGetObjectWaitObject(
    _In_ POBJECT_TYPE ObjectType,
    _In_ PVOID Object)
{
    ULONG_PTR WaitObjectInfo;
    ULONG WaitObjectFlags;

    if (!IsPointerOffset(ObjectType->DefaultObject))
    {
        return ObjectType->DefaultObject;
    }

    WaitObjectInfo = (ULONG_PTR)ObjectType->DefaultObject;
    if (!(WaitObjectInfo & 1))
    {
        return (PUCHAR)Object + WaitObjectInfo;
    }

    if (!(WaitObjectInfo & 2))
    {
        return (PVOID)(ULONG_PTR)ReadUnalignedUlongPtr((const ULONG_PTR*)((PUCHAR)Object + WaitObjectInfo - 1));
    }

    WaitObjectFlags = ReadUnalignedU32((const unsigned long*)((PUCHAR)Object + ObjectType->TypeInfo.WaitObjectFlagOffset));
    if ((WaitObjectFlags & ObjectType->TypeInfo.WaitObjectFlagMask) == ObjectType->TypeInfo.WaitObjectFlagMask)
    {
        return (PVOID)(ULONG_PTR)ReadUnalignedUlongPtr((const ULONG_PTR*)((PUCHAR)Object + ObjectType->TypeInfo.WaitObjectPointerOffset));
    }

    return (PUCHAR)Object + WaitObjectInfo - 3;
}

NTSTATUS
NTAPI
ObWaitForSingleObject(
    _In_ HANDLE ObjectHandle,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ KWAIT_REASON WaitReason,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PVOID Object;
    PVOID WaitObject;
    NTSTATUS Status;

    Status = ObReferenceObjectByHandle(ObjectHandle,
                                       SYNCHRONIZE,
                                       NULL,
                                       WaitMode,
                                       &Object,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    WaitObject = ObpGetObjectWaitObject(ObGetObjectType(Object), Object);
    Status = KeWaitForSingleObject(WaitObject,
                                   WaitReason,
                                   WaitMode,
                                   Alertable,
                                   Timeout);
    ObDereferenceObject(Object);
    return Status;
}

NTSTATUS
NTAPI
ObWaitForMultipleObjects(
    _In_ ULONG ObjectCount,
    _In_reads_(ObjectCount) PHANDLE ObjectHandles,
    _In_ WAIT_TYPE WaitType,
    _In_ KWAIT_REASON WaitReason,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PVOID Objects[MAXIMUM_WAIT_OBJECTS];
    PVOID WaitObjects[MAXIMUM_WAIT_OBJECTS];
    PKWAIT_BLOCK WaitBlocks = NULL;
    ULONG Index;
    NTSTATUS Status;

    if ((ObjectCount == 0) || (ObjectCount > MAXIMUM_WAIT_OBJECTS) ||
        (ObjectHandles == NULL) || ((WaitType != WaitAll) && (WaitType != WaitAny)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Objects, sizeof(Objects));
    if (ObjectCount > THREAD_WAIT_OBJECTS)
    {
        WaitBlocks = ExAllocatePoolZero(NonPagedPool,
                                        ObjectCount * sizeof(*WaitBlocks),
                                        TAG_WAIT);
        if (WaitBlocks == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = STATUS_SUCCESS;
    for (Index = 0; Index < ObjectCount; ++Index)
    {
        Status = ObReferenceObjectByHandle(ObjectHandles[Index],
                                           SYNCHRONIZE,
                                           NULL,
                                           WaitMode,
                                           &Objects[Index],
                                           NULL);
        if (!NT_SUCCESS(Status))
            break;

        WaitObjects[Index] = ObpGetObjectWaitObject(ObGetObjectType(Objects[Index]),
                                                    Objects[Index]);
    }

    if (NT_SUCCESS(Status))
    {
        Status = KeWaitForMultipleObjects(ObjectCount,
                                          WaitObjects,
                                          WaitType,
                                          WaitReason,
                                          WaitMode,
                                          Alertable,
                                          Timeout,
                                          WaitBlocks);
    }

    while (Index != 0)
        ObDereferenceObject(Objects[--Index]);
    if (WaitBlocks != NULL)
        ExFreePoolWithTag(WaitBlocks, TAG_WAIT);
    return Status;
}

/* SYSTEM CALL SUPPORT ********************************************************/

/*
 * A timeout and a handle array both live in the caller's memory. Probing is
 * only for user mode, but the read itself is guarded whatever the mode: a
 * driver that passes an unreadable pointer gets a status back, not a bug check.
 */
static
NTSTATUS
ObpFetchTimeout(
    _In_opt_ PLARGE_INTEGER Supplied,
    _In_ KPROCESSOR_MODE Mode,
    _Out_ PLARGE_INTEGER Storage,
    _Outptr_result_maybenull_ PLARGE_INTEGER *Effective)
{
    *Effective = NULL;

    if (Supplied == NULL) return STATUS_SUCCESS;

    _SEH2_TRY
    {
        if (Mode != KernelMode) ProbeForReadLargeInteger(Supplied);
        Storage->QuadPart = Supplied->QuadPart;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    *Effective = Storage;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpFetchHandleVector(
    _In_ PVOID Supplied,
    _In_ ULONG Entries,
    _In_ ULONG EntrySize,
    _In_ KPROCESSOR_MODE Mode,
    _Out_writes_(Entries) PHANDLE Storage)
{
    ULONG Index;

    _SEH2_TRY
    {
        if (Mode != KernelMode) ProbeForRead(Supplied, Entries * EntrySize, EntrySize);

        /* Taken away from the caller before anything is referenced, so no
         * entry can change between the check and the wait */
        if (EntrySize == sizeof(HANDLE))
        {
            RtlCopyMemory(Storage, Supplied, Entries * sizeof(HANDLE));
        }
        else
        {
            for (Index = 0; Index < Entries; ++Index)
                Storage[Index] = LongToHandle(((PLONG)Supplied)[Index]);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpCheckWaitShape(
    _In_ ULONG Entries,
    _In_ WAIT_TYPE WaitType)
{
    if ((Entries == 0) || (Entries > MAXIMUM_WAIT_OBJECTS))
        return STATUS_INVALID_PARAMETER_1;

    if ((WaitType != WaitAll) && (WaitType != WaitAny))
        return STATUS_INVALID_PARAMETER_3;

    return STATUS_SUCCESS;
}

/* Shared body of the native and the 32-bit multiple-object entry points. */
static
NTSTATUS
ObpWaitForHandleVector(
    _In_ PVOID Supplied,
    _In_ ULONG Entries,
    _In_ ULONG EntrySize,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ WAIT_TYPE WaitType,
    _In_ BOOLEAN Alertable)
{
    KPROCESSOR_MODE Mode = ExGetPreviousMode();
    HANDLE Vector[MAXIMUM_WAIT_OBJECTS];
    LARGE_INTEGER Storage;
    PLARGE_INTEGER Effective;
    NTSTATUS Status;

    Status = ObpCheckWaitShape(Entries, WaitType);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObpFetchHandleVector(Supplied, Entries, EntrySize, Mode, Vector);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObpFetchTimeout(Timeout, Mode, &Storage, &Effective);
    if (!NT_SUCCESS(Status)) return Status;

    return ObWaitForMultipleObjects(Entries, Vector, WaitType, UserRequest,
                                    Mode, Alertable, Effective);
}

/*
 * Signals one object and waits on another without letting go in between: the
 * release is told a wait follows, so the dispatcher carries the thread across.
 */
static
NTSTATUS
ObpSignalForHandoff(
    _In_ PVOID Object,
    _In_ ACCESS_MASK Granted)
{
    POBJECT_TYPE Type = ObGetObjectType(Object);
    NTSTATUS Status = STATUS_SUCCESS;

    if (Type == ExEventObjectType)
    {
        if (!(Granted & EVENT_MODIFY_STATE)) return STATUS_ACCESS_DENIED;

        KeSetEvent(Object, EVENT_INCREMENT, TRUE);
        return STATUS_SUCCESS;
    }

    if (Type == ExSemaphoreObjectType && !(Granted & SEMAPHORE_MODIFY_STATE))
        return STATUS_ACCESS_DENIED;

    if (Type != ExMutantObjectType && Type != ExSemaphoreObjectType)
        return STATUS_OBJECT_TYPE_MISMATCH;

    /* Both of these report a bad release by raising */
    _SEH2_TRY
    {
        if (Type == ExMutantObjectType)
            KeReleaseMutant(Object, MUTANT_INCREMENT, FALSE, TRUE);
        else
            KeReleaseSemaphore(Object, SEMAPHORE_INCREMENT, 1, TRUE);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtWaitForSingleObject(
    _In_ HANDLE ObjectHandle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER TimeOut)
{
    KPROCESSOR_MODE Mode = ExGetPreviousMode();
    PLARGE_INTEGER Effective;
    LARGE_INTEGER Storage;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObpFetchTimeout(TimeOut, Mode, &Storage, &Effective);
    if (!NT_SUCCESS(Status)) return Status;

    return ObWaitForSingleObject(ObjectHandle, Mode, UserRequest, Alertable, Effective);
}

NTSTATUS
NTAPI
NtWaitForMultipleObjects(
    _In_ ULONG ObjectCount,
    _In_reads_(ObjectCount) PHANDLE HandleArray,
    _In_ WAIT_TYPE WaitType,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER TimeOut)
{
    PAGED_CODE();

    return ObpWaitForHandleVector(HandleArray, ObjectCount, sizeof(HANDLE),
                                  TimeOut, WaitType, Alertable);
}

NTSTATUS
NTAPI
NtWaitForMultipleObjects32(
    _In_ ULONG ObjectCount,
    _In_reads_(ObjectCount) PLONG HandleArray,
    _In_ WAIT_TYPE WaitType,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER TimeOut)
{
    PAGED_CODE();

    /* The 32-bit entry point carries its handles as LONGs */
    return ObpWaitForHandleVector(HandleArray, ObjectCount, sizeof(LONG),
                                  TimeOut, WaitType, Alertable);
}

NTSTATUS
NTAPI
NtSignalAndWaitForSingleObject(
    _In_ HANDLE SignalObject,
    _In_ HANDLE WaitObject,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER TimeOut)
{
    KPROCESSOR_MODE Mode = ExGetPreviousMode();
    OBJECT_HANDLE_INFORMATION Information;
    PVOID Signalled, Awaited, Target;
    PLARGE_INTEGER Effective;
    LARGE_INTEGER Storage;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObpFetchTimeout(TimeOut, Mode, &Storage, &Effective);
    if (!NT_SUCCESS(Status)) return Status;

    /* No access is demanded up front: which right the signal needs depends on
     * the type, and only the type knows */
    Status = ObReferenceObjectByHandle(SignalObject, 0, NULL, Mode,
                                       &Signalled, &Information);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(WaitObject, SYNCHRONIZE, NULL, Mode,
                                       &Awaited, NULL);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Signalled);
        return Status;
    }

    /* Resolved first: nothing may come between the signal and the wait */
    Target = ObpGetObjectWaitObject(ObGetObjectType(Awaited), Awaited);

    Status = ObpSignalForHandoff(Signalled, Information.GrantedAccess);
    if (NT_SUCCESS(Status))
    {
        Status = KeWaitForSingleObject(Target, UserRequest, Mode, Alertable, Effective);
    }

    ObDereferenceObject(Awaited);
    ObDereferenceObject(Signalled);

    return Status;
}

/* EOF */
