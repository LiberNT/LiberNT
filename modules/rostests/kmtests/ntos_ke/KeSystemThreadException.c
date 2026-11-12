/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Stops the system with a fault in a system thread, so that the report of that stop can be checked
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

static
DECLSPEC_NOINLINE
VOID
NTAPI
FaultingSystemThread(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    *(volatile ULONG *)(ULONG_PTR)0x10 = 0;
}

START_TEST(KeSystemThreadException)
{
    LARGE_INTEGER Timeout;
    HANDLE ThreadHandle;
    NTSTATUS Status;

    Status = PsCreateSystemThread(&ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, FaultingSystemThread, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Timeout.QuadPart = -10LL * 1000 * 1000 * 10;
    Status = ZwWaitForSingleObject(ThreadHandle, FALSE, &Timeout);
    ok(FALSE, "the system did not stop: 0x%08lx\n", Status);
    ZwClose(ThreadHandle);
}
