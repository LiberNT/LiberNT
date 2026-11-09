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

/* PRIVATE FUNCTIONS **********************************************************/

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

static
NTSTATUS
ObpCaptureWaitTimeout(
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PLARGE_INTEGER Captured,
    _Outptr_result_maybenull_ PLARGE_INTEGER *CapturedTimeout)
{
    *CapturedTimeout = NULL;

    if (Timeout == NULL) return STATUS_SUCCESS;

    /* A kernel-mode caller is not probed, but it still gets a status back
     * instead of a bug check when its pointer is unreadable */
    _SEH2_TRY
    {
        if (PreviousMode != KernelMode) ProbeForReadLargeInteger(Timeout);
        *Captured = *Timeout;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    *CapturedTimeout = Captured;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ObpSignalObjectForWait(
    _In_ PVOID Object,
    _In_ ACCESS_MASK GrantedAccess)
{
    POBJECT_TYPE ObjectType = ObGetObjectType(Object);
    NTSTATUS Status = STATUS_SUCCESS;

    /* The release runs with Wait set, so the dispatcher hands this thread
     * straight to the wait that follows it without dropping the lock */
    if (ObjectType == ExEventObjectType)
    {
        if (!(GrantedAccess & EVENT_MODIFY_STATE)) return STATUS_ACCESS_DENIED;
        KeSetEvent(Object, EVENT_INCREMENT, TRUE);
    }
    else if (ObjectType == ExMutantObjectType)
    {
        _SEH2_TRY
        {
            KeReleaseMutant(Object, MUTANT_INCREMENT, FALSE, TRUE);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else if (ObjectType == ExSemaphoreObjectType)
    {
        if (!(GrantedAccess & SEMAPHORE_MODIFY_STATE)) return STATUS_ACCESS_DENIED;

        _SEH2_TRY
        {
            KeReleaseSemaphore(Object, SEMAPHORE_INCREMENT, 1, TRUE);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        Status = STATUS_OBJECT_TYPE_MISMATCH;
    }

    return Status;
}

/* PUBLIC FUNCTIONS ***********************************************************/

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

    /* The thread only carries enough built-in wait blocks for a short wait */
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

/* SYSTEM CALLS ***************************************************************/

NTSTATUS
NTAPI
NtWaitForSingleObject(
    _In_ HANDLE ObjectHandle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER TimeOut)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER CapturedTimeout;
    PLARGE_INTEGER Timeout;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObpCaptureWaitTimeout(TimeOut, PreviousMode, &CapturedTimeout, &Timeout);
    if (!NT_SUCCESS(Status)) return Status;

    return ObWaitForSingleObject(ObjectHandle,
                                 PreviousMode,
                                 UserRequest,
                                 Alertable,
                                 Timeout);
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
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handles[MAXIMUM_WAIT_OBJECTS];
    LARGE_INTEGER CapturedTimeout;
    PLARGE_INTEGER Timeout;
    NTSTATUS Status;

    PAGED_CODE();

    if ((ObjectCount == 0) || (ObjectCount > MAXIMUM_WAIT_OBJECTS))
        return STATUS_INVALID_PARAMETER_1;

    if ((WaitType != WaitAll) && (WaitType != WaitAny))
        return STATUS_INVALID_PARAMETER_3;

    /* Take our own copy: the caller must not be able to swap a handle under
     * the references we are about to take */
    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead(HandleArray, ObjectCount * sizeof(HANDLE), sizeof(HANDLE));
        RtlCopyMemory(Handles, HandleArray, ObjectCount * sizeof(HANDLE));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = ObpCaptureWaitTimeout(TimeOut, PreviousMode, &CapturedTimeout, &Timeout);
    if (!NT_SUCCESS(Status)) return Status;

    return ObWaitForMultipleObjects(ObjectCount,
                                    Handles,
                                    WaitType,
                                    UserRequest,
                                    PreviousMode,
                                    Alertable,
                                    Timeout);
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
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handles[MAXIMUM_WAIT_OBJECTS];
    LARGE_INTEGER CapturedTimeout;
    PLARGE_INTEGER Timeout;
    NTSTATUS Status;
    ULONG Index;

    PAGED_CODE();

    if ((ObjectCount == 0) || (ObjectCount > MAXIMUM_WAIT_OBJECTS))
        return STATUS_INVALID_PARAMETER_1;

    if ((WaitType != WaitAll) && (WaitType != WaitAny))
        return STATUS_INVALID_PARAMETER_3;

    /* The 32-bit entry point carries the handles as LONGs; widen each one the
     * way a 32-bit caller's handle values map onto this address width */
    _SEH2_TRY
    {
        if (PreviousMode != KernelMode)
            ProbeForRead(HandleArray, ObjectCount * sizeof(LONG), sizeof(LONG));
        for (Index = 0; Index < ObjectCount; ++Index)
            Handles[Index] = LongToHandle(HandleArray[Index]);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    Status = ObpCaptureWaitTimeout(TimeOut, PreviousMode, &CapturedTimeout, &Timeout);
    if (!NT_SUCCESS(Status)) return Status;

    return ObWaitForMultipleObjects(ObjectCount,
                                    Handles,
                                    WaitType,
                                    UserRequest,
                                    PreviousMode,
                                    Alertable,
                                    Timeout);
}

NTSTATUS
NTAPI
NtSignalAndWaitForSingleObject(
    _In_ HANDLE SignalObject,
    _In_ HANDLE WaitObject,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER TimeOut)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    OBJECT_HANDLE_INFORMATION HandleInfo;
    LARGE_INTEGER CapturedTimeout;
    PLARGE_INTEGER Timeout;
    PVOID SignalObj, WaitObj, WaitTarget;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObpCaptureWaitTimeout(TimeOut, PreviousMode, &CapturedTimeout, &Timeout);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(SignalObject,
                                       0,
                                       NULL,
                                       PreviousMode,
                                       &SignalObj,
                                       &HandleInfo);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObReferenceObjectByHandle(WaitObject,
                                       SYNCHRONIZE,
                                       NULL,
                                       PreviousMode,
                                       &WaitObj,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(SignalObj);
        return Status;
    }

    /* Resolve the wait target first: once the signal is given with Wait set,
     * nothing may come between it and the wait it hands over to */
    WaitTarget = ObpGetObjectWaitObject(ObGetObjectType(WaitObj), WaitObj);

    Status = ObpSignalObjectForWait(SignalObj, HandleInfo.GrantedAccess);
    if (NT_SUCCESS(Status))
    {
        Status = KeWaitForSingleObject(WaitTarget,
                                       UserRequest,
                                       PreviousMode,
                                       Alertable,
                                       Timeout);
    }

    ObDereferenceObject(WaitObj);
    ObDereferenceObject(SignalObj);
    return Status;
}

/* EOF */
