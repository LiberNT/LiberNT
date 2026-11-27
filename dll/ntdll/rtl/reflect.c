/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Process reflection
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#define RTL_PROCESS_REFLECTION_FLAGS_VALID \
    (RTL_PROCESS_REFLECTION_FLAGS_INHERIT_HANDLES | RTL_PROCESS_REFLECTION_FLAGS_NO_SUSPEND | \
     RTL_PROCESS_REFLECTION_FLAGS_NO_SYNCHRONIZE | RTL_PROCESS_REFLECTION_FLAGS_NO_CLOSE_EVENT)

NTSTATUS
NTAPI
RtlCreateProcessReflection(
    _In_ HANDLE ProcessHandle,
    _In_ ULONG Flags,
    _In_opt_ PVOID StartRoutine,
    _In_opt_ PVOID StartContext,
    _In_opt_ HANDLE EventHandle,
    _Out_opt_ PRTLP_PROCESS_REFLECTION_REFLECTION_INFORMATION ReflectionInformation)
{
    HANDLE Process = NULL, Thread = NULL;
    CLIENT_ID ClientId = { NULL, NULL };
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(EventHandle);

    if ((Flags & ~RTL_PROCESS_REFLECTION_FLAGS_VALID) != 0)
        return STATUS_INVALID_PARAMETER_2;

    /* With no section the new process gets a copy of the address space of the one it is made from */
    Status = NtCreateProcessEx(&Process,
                               PROCESS_ALL_ACCESS,
                               NULL,
                               ProcessHandle,
                               (Flags & RTL_PROCESS_REFLECTION_FLAGS_INHERIT_HANDLES) ? PROCESS_CREATE_FLAGS_INHERIT_HANDLES : 0,
                               NULL,
                               NULL,
                               NULL,
                               0);
    if (!NT_SUCCESS(Status)) return Status;
    ASSERT(Process != NULL);

    /* TODO: the event a caller hands over is neither signalled nor closed, and NO_SYNCHRONIZE/NO_CLOSE_EVENT change nothing */
    /* FIXME: a thread that runs in the reflection meets the loader's unimplemented fork path and ends it with STATUS_NOT_IMPLEMENTED instead of running StartRoutine */
    Status = RtlCreateUserThread(Process,
                                 NULL,
                                 TRUE,
                                 0,
                                 0,
                                 0,
                                 StartRoutine ? (PTHREAD_START_ROUTINE)StartRoutine : (PTHREAD_START_ROUTINE)RtlExitUserThread,
                                 StartRoutine ? StartContext : (PVOID)(ULONG_PTR)STATUS_SUCCESS,
                                 &Thread,
                                 &ClientId);
    if (!NT_SUCCESS(Status))
    {
        NtTerminateProcess(Process, Status);
        NtClose(Process);
        return Status;
    }
    ASSERT(Thread != NULL);

    if (Flags & RTL_PROCESS_REFLECTION_FLAGS_NO_SUSPEND)
        NtResumeThread(Thread, NULL);

    if (ReflectionInformation == NULL)
    {
        NtClose(Thread);
        NtClose(Process);
        return STATUS_SUCCESS;
    }

    ReflectionInformation->ReflectionProcessHandle = Process;
    ReflectionInformation->ReflectionThreadHandle = Thread;
    ReflectionInformation->ReflectionClientId = ClientId;
    return STATUS_SUCCESS;
}
