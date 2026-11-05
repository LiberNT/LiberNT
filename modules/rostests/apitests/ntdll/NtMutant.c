
#include "precomp.h"

VOID
Test_Release(VOID)
{
    HANDLE MutantHandle;
    NTSTATUS Status;

    Status = NtCreateMutant(&MutantHandle, 0, NULL, TRUE);
    ok_ntstatus(Status, STATUS_SUCCESS);

    Status = NtReleaseMutant(MutantHandle, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);

}



static DWORD WINAPI
AbandonMutantThread(PVOID Context)
{
    return WaitForSingleObject((HANDLE)Context, 5000) == WAIT_OBJECT_0 ? 0 : 1;
}

static VOID
Test_ReleaseAbandoned(VOID)
{
    HANDLE MutantHandle = NULL, Thread;
    NTSTATUS Status;
    DWORD WaitResult, ExitCode;

    Status = NtCreateMutant(&MutantHandle, MUTANT_ALL_ACCESS, NULL, FALSE);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Thread = CreateThread(NULL, 0, AbandonMutantThread, MutantHandle, 0, NULL);
    ok(Thread != NULL, "CreateThread failed %lu\n", GetLastError());
    if (!Thread)
    {
        NtClose(MutantHandle);
        return;
    }

    WaitResult = WaitForSingleObject(Thread, 10000);
    ok(WaitResult == WAIT_OBJECT_0, "Owner thread wait returned %lu\n", WaitResult);
    if (WaitResult != WAIT_OBJECT_0)
    {
        CloseHandle(Thread);
        return;
    }
    ok(GetExitCodeThread(Thread, &ExitCode) && ExitCode == 0,
       "Owner thread failed to acquire the mutant\n");
    Status = NtReleaseMutant(MutantHandle, NULL);
    ok_ntstatus(Status, STATUS_ABANDONED);

    WaitResult = WaitForSingleObject(MutantHandle, 0);
    ok(WaitResult == WAIT_ABANDONED, "Abandoned wait returned %lu\n", WaitResult);
    if (WaitResult == WAIT_ABANDONED)
    {
        Status = NtReleaseMutant(MutantHandle, NULL);
        ok_ntstatus(Status, STATUS_SUCCESS);
        Status = NtReleaseMutant(MutantHandle, NULL);
        ok_ntstatus(Status, STATUS_MUTANT_NOT_OWNED);
    }
    CloseHandle(Thread);
    NtClose(MutantHandle);
}

START_TEST(NtMutant)
{
    Test_Release();
    Test_ReleaseAbandoned();
}
