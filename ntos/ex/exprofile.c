/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Execution profiles: counting where a range of code spends time
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

POBJECT_TYPE ExProfileObjectType = NULL;

KMUTANT ExpProfileMutex;

GENERIC_MAPPING ExpProfileMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE,
    STANDARD_RIGHTS_ALL
};

/* PRIVATE DATA ***************************************************************/

#define EXP_PROFILE_TAG 'forP'

/* The smallest and largest number of buckets a profile may ask for */
#define EXP_PROFILE_MINIMUM_BUCKETS 2

/* PRIVATE FUNCTIONS **********************************************************/

/*
 * Lets go of everything a profile was holding: the pages of the caller's buffer
 * it had locked down, and the process it was watching.
 */
VOID
NTAPI
ExpDeleteProfile(
    _In_ PVOID ObjectBody)
{
    PEPROFILE Profile = ObjectBody;

    if (Profile->LockedBufferAddress != NULL)
    {
        KeStopProfile(Profile->ProfileObject);

        ExUnlockUserBuffer(Profile->Mdl);

        Profile->LockedBufferAddress = NULL;
        Profile->Mdl = NULL;
    }

    if (Profile->ProfileObject != NULL)
    {
        ExFreePoolWithTag(Profile->ProfileObject, EXP_PROFILE_TAG);
        Profile->ProfileObject = NULL;
    }

    if (Profile->Process != NULL)
    {
        ObDereferenceObject(Profile->Process);
        Profile->Process = NULL;
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpInitializeProfileImplementation(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer;
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"Profile");

    RtlZeroMemory(&Initializer, sizeof(Initializer));

    Initializer.Length = sizeof(Initializer);
    Initializer.InvalidAttributes = OBJ_OPENLINK | OBJ_PERMANENT;
    Initializer.ValidAccessMask = STANDARD_RIGHTS_ALL;
    Initializer.GenericMapping = ExpProfileMapping;
    Initializer.PoolType = NonPagedPool;
    Initializer.DefaultNonPagedPoolCharge = sizeof(EPROFILE);
    Initializer.DeleteProcedure = ExpDeleteProfile;

    KeInitializeMutant(&ExpProfileMutex, FALSE);

    return NT_SUCCESS(ObCreateObjectType(&Name,
                                         &Initializer,
                                         NULL,
                                         &ExProfileObjectType));
}

NTSTATUS
NTAPI
NtCreateProfile(
    _Out_ PHANDLE ProfileHandle,
    _In_opt_ HANDLE ProcessHandle,
    _In_ PVOID ImageBase,
    _In_ SIZE_T ImageSize,
    _In_ ULONG Granularity,
    _Out_ PVOID Buffer,
    _In_ ULONG ProfilingSize,
    _In_ KPROFILE_SOURCE Source,
    _In_ KAFFINITY ProcessorMask)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROFILE Profile;
    PEPROCESS Process = NULL;
    HANDLE Handle;
    ULONG Buckets;
    NTSTATUS Status;

    PAGED_CODE();

    if (ImageSize == 0)
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    /*
     * The buffer holds one count for each bucket, and a bucket covers a
     * power-of-two run of addresses given by the granularity.
     */
    if (ProfilingSize < EXP_PROFILE_MINIMUM_BUCKETS * sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER_7;
    }

    Buckets = ProfilingSize / sizeof(ULONG);

    if ((ULONG_PTR)ImageSize > ((ULONG_PTR)Buckets << Granularity))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(ProfileHandle);
            ProbeForWrite(Buffer, ProfilingSize, sizeof(ULONG));
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    if (ProcessHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(ProcessHandle,
                                           PROCESS_QUERY_INFORMATION,
                                           PsProcessType,
                                           PreviousMode,
                                           (PVOID *)&Process,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    Status = ObCreateObject(PreviousMode,
                            ExProfileObjectType,
                            NULL,
                            PreviousMode,
                            NULL,
                            sizeof(EPROFILE),
                            0,
                            0,
                            (PVOID *)&Profile);
    if (!NT_SUCCESS(Status))
    {
        if (Process != NULL)
        {
            ObDereferenceObject(Process);
        }

        return Status;
    }

    RtlZeroMemory(Profile, sizeof(EPROFILE));

    Profile->Process = Process;
    Profile->RangeBase = ImageBase;
    Profile->RangeSize = ImageSize;
    Profile->Buffer = Buffer;
    Profile->BufferSize = ProfilingSize;
    Profile->BucketSize = Granularity;
    Profile->ProfileSource = Source;
    Profile->Affinity = ProcessorMask;

    Profile->ProfileObject = ExAllocatePoolZero(NonPagedPool,
                                                sizeof(KPROFILE),
                                                EXP_PROFILE_TAG);
    if (Profile->ProfileObject == NULL)
    {
        ObDereferenceObject(Profile);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ObInsertObject(Profile, NULL, 0, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *ProfileHandle = Handle;
        }
        _SEH2_EXCEPT(ExSystemExceptionFilter())
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        return Status;
    }

    *ProfileHandle = Handle;
    return Status;
}

/*
 * Starts counting. The caller's buffer is locked down and mapped where the
 * kernel can reach it, because the counts are written at a level at which no
 * page may be missing.
 */
NTSTATUS
NTAPI
NtStartProfile(
    _In_ HANDLE ProfileHandle)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    PEPROFILE Profile;
    PVOID MappedAddress;
    PMDL Mdl;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ProfileHandle,
                                       0,
                                       ExProfileObjectType,
                                       PreviousMode,
                                       (PVOID *)&Profile,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeWaitForSingleObject(&ExpProfileMutex,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    if (Profile->LockedBufferAddress != NULL)
    {
        KeReleaseMutant(&ExpProfileMutex, MUTANT_INCREMENT, FALSE, FALSE);
        ObDereferenceObject(Profile);
        return STATUS_PROFILING_AT_LIMIT;
    }

    Status = ExLockUserBuffer(Profile->Buffer,
                              Profile->BufferSize,
                              PreviousMode,
                              IoWriteAccess,
                              &MappedAddress,
                              &Mdl);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseMutant(&ExpProfileMutex, MUTANT_INCREMENT, FALSE, FALSE);
        ObDereferenceObject(Profile);
        return Status;
    }

    Profile->LockedBufferAddress = MappedAddress;
    Profile->Mdl = Mdl;

    KeInitializeProfile(Profile->ProfileObject,
                        (Profile->Process != NULL) ?
                        &Profile->Process->Pcb : NULL,
                        Profile->RangeBase,
                        Profile->RangeSize,
                        Profile->BucketSize,
                        Profile->ProfileSource,
                        Profile->Affinity);

    KeStartProfile(Profile->ProfileObject, MappedAddress);

    KeReleaseMutant(&ExpProfileMutex, MUTANT_INCREMENT, FALSE, FALSE);

    ObDereferenceObject(Profile);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtStopProfile(
    _In_ HANDLE ProfileHandle)
{
    PEPROFILE Profile;
    NTSTATUS Status;

    PAGED_CODE();

    Status = ObReferenceObjectByHandle(ProfileHandle,
                                       0,
                                       ExProfileObjectType,
                                       ExGetPreviousMode(),
                                       (PVOID *)&Profile,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeWaitForSingleObject(&ExpProfileMutex,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    if (Profile->LockedBufferAddress == NULL)
    {
        KeReleaseMutant(&ExpProfileMutex, MUTANT_INCREMENT, FALSE, FALSE);
        ObDereferenceObject(Profile);
        return STATUS_SUCCESS;
    }

    KeStopProfile(Profile->ProfileObject);

    ExUnlockUserBuffer(Profile->Mdl);

    Profile->LockedBufferAddress = NULL;
    Profile->Mdl = NULL;

    KeReleaseMutant(&ExpProfileMutex, MUTANT_INCREMENT, FALSE, FALSE);

    ObDereferenceObject(Profile);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtQueryIntervalProfile(
    _In_ KPROFILE_SOURCE ProfileSource,
    _Out_ PULONG Interval)
{
    ULONG Value;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    Value = KeQueryIntervalProfile(ProfileSource);

    if (ExGetPreviousMode() == KernelMode)
    {
        *Interval = Value;
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForWriteUlong(Interval);
        *Interval = Value;
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

NTSTATUS
NTAPI
NtSetIntervalProfile(
    _In_ ULONG Interval,
    _In_ KPROFILE_SOURCE ClockSource)
{
    PAGED_CODE();

    KeSetIntervalProfile(Interval, ClockSource);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtQueryPerformanceCounter(
    _Out_ PLARGE_INTEGER Counter,
    _Out_opt_ PLARGE_INTEGER Frequency)
{
    LARGE_INTEGER Count, Rate;
    NTSTATUS Status = STATUS_SUCCESS;

    Count = KeQueryPerformanceCounter(&Rate);

    if (ExGetPreviousMode() == KernelMode)
    {
        *Counter = Count;
        if (Frequency != NULL) *Frequency = Rate;
        return STATUS_SUCCESS;
    }

    _SEH2_TRY
    {
        ProbeForWriteLargeInteger(Counter);
        *Counter = Count;

        if (Frequency != NULL)
        {
            ProbeForWriteLargeInteger(Frequency);
            *Frequency = Rate;
        }
    }
    _SEH2_EXCEPT(ExSystemExceptionFilter())
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/*
 * The auxiliary counter is a second clock some machines offer that runs at a
 * steady rate of its own. Without such a clock there is nothing to convert
 * between, which is what callers are told.
 */
NTSTATUS
NTAPI
NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(
    _In_ ULONG ConversionType,
    _In_ PULONGLONG FromValue,
    _Out_opt_ PULONGLONG ToValue,
    _Out_opt_ PULONGLONG ConversionError)
{
    UNREFERENCED_PARAMETER(ConversionType);
    UNREFERENCED_PARAMETER(FromValue);
    UNREFERENCED_PARAMETER(ToValue);
    UNREFERENCED_PARAMETER(ConversionError);

    /* TODO: no auxiliary counter is exposed by the kernel to convert against. */
    return STATUS_NOT_SUPPORTED;
}

/* EOF */
