/*
 * PROJECT:         ReactOS NT Library
 * FILE:            dll/ntdll/rtl/exit.c
 * PURPOSE:         RtlExitUserProcess implementation
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

static VOID
RtlpWaitForOtherThreads(VOID)
{
    THREAD_BASIC_INFORMATION Information;
    HANDLE Thread = NULL, Next;

    while (NT_SUCCESS(NtGetNextThread(NtCurrentProcess(), Thread, SYNCHRONIZE | THREAD_QUERY_INFORMATION, 0, 0, &Next)))
    {
        if (Thread != NULL)
            NtClose(Thread);
        Thread = Next;
        if (NT_SUCCESS(NtQueryInformationThread(Thread, ThreadBasicInformation, &Information, sizeof(Information), NULL)) &&
            Information.ClientId.UniqueThread != NtCurrentTeb()->ClientId.UniqueThread)
        {
            NtWaitForSingleObject(Thread, FALSE, NULL);
        }
    }
    if (Thread != NULL)
        NtClose(Thread);
}

/*
 * @implemented
 *
 * Performs orderly process shutdown: ends the other threads, detaches all
 * loaded DLLs via LdrShutdownProcess, then terminates the process via
 * NtTerminateProcess.
 */
VOID
NTAPI
RtlExitUserProcess(NTSTATUS Status)
{
    RtlEnterCriticalSection(&LdrpLoaderLock);
    RtlAcquirePebLock();
    NtTerminateProcess(NULL, Status);
    RtlpWaitForOtherThreads();

    /* Call the Loader to send DLL_PROCESS_DETACH notifications */
    LdrShutdownProcess();

    /* Terminate the process. NtTerminateProcess does not return. */
    NtTerminateProcess(NtCurrentProcess(), Status);

    /* Not reached */
    for (;;)
        ;
}
