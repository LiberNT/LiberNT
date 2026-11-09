/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Semaphores and mutants as nameable objects
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE ExSemaphoreObjectType = NULL;
POBJECT_TYPE ExMutantObjectType = NULL;

GENERIC_MAPPING ExSemaphoreMapping =
{
    STANDARD_RIGHTS_READ | SEMAPHORE_QUERY_STATE,
    STANDARD_RIGHTS_WRITE | SEMAPHORE_MODIFY_STATE,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    SEMAPHORE_ALL_ACCESS
};

GENERIC_MAPPING ExpMutantMapping =
{
    STANDARD_RIGHTS_READ | MUTANT_QUERY_STATE,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE,
    MUTANT_ALL_ACCESS
};

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeSemaphoreImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Semaphore");

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.ValidAccessMask = SEMAPHORE_ALL_ACCESS;
    Initializer.GenericMapping = ExSemaphoreMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.DefaultNonPagedPoolCharge = sizeof(KSEMAPHORE);

    return NT_SUCCESS(ObCreateObjectType(&Name,
                                         &Initializer,
                                         NULL,
                                         &ExSemaphoreObjectType));
}

NTSTATUS
NTAPI
NtCreateSemaphore(
    _Out_ PHANDLE SemaphoreHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ LONG InitialCount,
    _In_ LONG MaximumCount)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKSEMAPHORE Semaphore;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if ((MaximumCount <= 0) || (InitialCount < 0) ||
        (InitialCount > MaximumCount))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(SemaphoreHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObCreateObject(PreviousMode,
                            ExSemaphoreObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(KSEMAPHORE),
                            0,
                            0,
                            (PVOID *)&Semaphore);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeInitializeSemaphore(Semaphore, InitialCount, MaximumCount);

    Status = ObInsertObject(Semaphore, NULL, DesiredAccess, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *SemaphoreHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *SemaphoreHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtOpenSemaphore(
    _Out_ PHANDLE SemaphoreHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(SemaphoreHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                ExSemaphoreObjectType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *SemaphoreHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *SemaphoreHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtReleaseSemaphore(
    _In_ HANDLE SemaphoreHandle,
    _In_ LONG ReleaseCount,
    _Out_opt_ PLONG PreviousCount)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKSEMAPHORE Semaphore;
    LONG Count;
    NTSTATUS Status;

    PAGED_CODE();

    if (ReleaseCount <= 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((PreviousCount != NULL) && (PreviousMode != KernelMode))
    {
        _SEH2_TRY
        {
            ProbeForWriteLong(PreviousCount);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(SemaphoreHandle,
                                       SEMAPHORE_MODIFY_STATE,
                                       ExSemaphoreObjectType,
                                       PreviousMode,
                                       (PVOID *)&Semaphore,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    _SEH2_TRY
    {
        Count = KeReleaseSemaphore(Semaphore, SEMAPHORE_INCREMENT,
                                   ReleaseCount, FALSE);

        if (PreviousCount != NULL)
        {
            *PreviousCount = Count;
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(Semaphore);

    return Status;
}

NTSTATUS
NTAPI
NtQuerySemaphore(
    _In_ HANDLE SemaphoreHandle,
    _In_ SEMAPHORE_INFORMATION_CLASS SemaphoreInformationClass,
    _Out_ PVOID SemaphoreInformation,
    _In_ ULONG SemaphoreInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKSEMAPHORE Semaphore;
    SEMAPHORE_BASIC_INFORMATION Basic;
    NTSTATUS Status;

    PAGED_CODE();

    if (SemaphoreInformationClass != SemaphoreBasicInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (SemaphoreInformationLength < sizeof(SEMAPHORE_BASIC_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(SemaphoreInformation,
                          sizeof(SEMAPHORE_BASIC_INFORMATION),
                          sizeof(ULONG));
            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(SemaphoreHandle,
                                       SEMAPHORE_QUERY_STATE,
                                       ExSemaphoreObjectType,
                                       PreviousMode,
                                       (PVOID *)&Semaphore,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Basic.CurrentCount = KeReadStateSemaphore(Semaphore);
    Basic.MaximumCount = Semaphore->Limit;

    ObDereferenceObject(Semaphore);

    _SEH2_TRY
    {
        *(PSEMAPHORE_BASIC_INFORMATION)SemaphoreInformation = Basic;
        if (ReturnLength != NULL)
        {
            *ReturnLength = sizeof(SEMAPHORE_BASIC_INFORMATION);
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* MUTANTS ********************************************************************/

/*
 * A thread that dies still owning a mutant leaves it abandoned rather than
 * locked for good, so the next waiter is told what happened and carries on.
 */
VOID
NTAPI
ExpDeleteMutant(
    _In_ PVOID ObjectBody)
{
    PKMUTANT Mutant = ObjectBody;

    KeReleaseMutant(Mutant, MUTANT_INCREMENT, TRUE, FALSE);
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeMutantImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Mutant");

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.ValidAccessMask = MUTANT_ALL_ACCESS;
    Initializer.GenericMapping = ExpMutantMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.DefaultNonPagedPoolCharge = sizeof(KMUTANT);
    Initializer.DeleteProcedure = ExpDeleteMutant;

    return NT_SUCCESS(ObCreateObjectType(&Name,
                                         &Initializer,
                                         NULL,
                                         &ExMutantObjectType));
}

NTSTATUS
NTAPI
NtCreateMutant(
    _Out_ PHANDLE MutantHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ BOOLEAN InitialOwner)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKMUTANT Mutant;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(MutantHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObCreateObject(PreviousMode,
                            ExMutantObjectType,
                            ObjectAttributes,
                            PreviousMode,
                            NULL,
                            sizeof(KMUTANT),
                            0,
                            0,
                            (PVOID *)&Mutant);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeInitializeMutant(Mutant, InitialOwner);

    Status = ObInsertObject(Mutant, NULL, DesiredAccess, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *MutantHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *MutantHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtOpenMutant(
    _Out_ PHANDLE MutantHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(MutantHandle);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObOpenObjectByName(ObjectAttributes,
                                ExMutantObjectType,
                                PreviousMode,
                                NULL,
                                DesiredAccess,
                                NULL,
                                &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *MutantHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *MutantHandle = Handle;
    return Status;
}

NTSTATUS
NTAPI
NtReleaseMutant(
    _In_ HANDLE MutantHandle,
    _Out_opt_ PLONG PreviousCount)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKMUTANT Mutant;
    LONG Count;
    NTSTATUS Status;

    PAGED_CODE();

    if ((PreviousCount != NULL) && (PreviousMode != KernelMode))
    {
        _SEH2_TRY
        {
            ProbeForWriteLong(PreviousCount);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(MutantHandle,
                                       0,
                                       ExMutantObjectType,
                                       PreviousMode,
                                       (PVOID *)&Mutant,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    _SEH2_TRY
    {
        Count = KeReleaseMutant(Mutant, MUTANT_INCREMENT, FALSE, FALSE);

        if (PreviousCount != NULL)
        {
            *PreviousCount = Count;
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    ObDereferenceObject(Mutant);

    return Status;
}

NTSTATUS
NTAPI
NtQueryMutant(
    _In_ HANDLE MutantHandle,
    _In_ MUTANT_INFORMATION_CLASS MutantInformationClass,
    _Out_ PVOID MutantInformation,
    _In_ ULONG MutantInformationLength,
    _Out_opt_ PULONG ReturnLength)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PKMUTANT Mutant;
    MUTANT_BASIC_INFORMATION Basic;
    NTSTATUS Status;

    PAGED_CODE();

    if (MutantInformationClass != MutantBasicInformation)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (MutantInformationLength < sizeof(MUTANT_BASIC_INFORMATION))
    {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(MutantInformation,
                          sizeof(MUTANT_BASIC_INFORMATION),
                          sizeof(ULONG));
            if (ReturnLength != NULL) ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = ObReferenceObjectByHandle(MutantHandle,
                                       MUTANT_QUERY_STATE,
                                       ExMutantObjectType,
                                       PreviousMode,
                                       (PVOID *)&Mutant,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Basic.CurrentCount = KeReadStateMutant(Mutant);
    Basic.OwnedByCaller = (Mutant->OwnerThread == KeGetCurrentThread());
    Basic.AbandonedState = Mutant->Abandoned;

    ObDereferenceObject(Mutant);

    _SEH2_TRY
    {
        *(PMUTANT_BASIC_INFORMATION)MutantInformation = Basic;
        if (ReturnLength != NULL)
        {
            *ReturnLength = sizeof(MUTANT_BASIC_INFORMATION);
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/* EOF */
