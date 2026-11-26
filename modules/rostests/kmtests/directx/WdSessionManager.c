/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The watchdog session manager runs the registered session change callouts when win32k reports a session
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

NTSYSAPI
PVOID
NTAPI
RtlFindExportedRoutineByName(
    _In_ PVOID ImageBase,
    _In_ PCSTR RoutineName);

typedef NTSTATUS (NTAPI *PFN_SMGR_REGISTER)(PVOID, PVOID, ULONG_PTR, PVOID, ULONG);
typedef NTSTATUS (NTAPI *PFN_SMGR_UNREGISTER)(PVOID);
typedef NTSTATUS (NTAPI *PFN_SMGR_NOTIFY)(PVOID, PVOID, PVOID, PVOID, ULONG);

static volatile LONG CalloutCount;
static PVOID CalloutParameters[4];
static UCHAR RegistrationData[16];

static
NTSTATUS
NTAPI
TestSessionCallout(
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Parameter3,
    _In_opt_ PVOID Parameter4)
{
    CalloutParameters[0] = Parameter1;
    CalloutParameters[1] = Parameter2;
    CalloutParameters[2] = Parameter3;
    CalloutParameters[3] = Parameter4;
    InterlockedIncrement(&CalloutCount);
    return STATUS_SUCCESS;
}

static
PVOID
FindWatchdog(VOID)
{
    PRTL_PROCESS_MODULES Modules;
    PVOID Base = NULL;
    ULONG Length = 0;
    ULONG Index;
    NTSTATUS Status;

    Status = ZwQuerySystemInformation(SystemModuleInformation, NULL, 0, &Length);
    if (Status != STATUS_INFO_LENGTH_MISMATCH || Length == 0)
        return NULL;

    Modules = ExAllocatePoolWithTag(PagedPool, Length, 'mSdW');
    if (Modules == NULL)
        return NULL;

    Status = ZwQuerySystemInformation(SystemModuleInformation, Modules, Length, &Length);
    for (Index = 0; NT_SUCCESS(Status) && Index < Modules->NumberOfModules; Index++)
    {
        PRTL_PROCESS_MODULE_INFORMATION Module = &Modules->Modules[Index];

        if (_stricmp((PCSTR)Module->FullPathName + Module->OffsetToFileName, "watchdog.sys") == 0)
        {
            Base = Module->ImageBase;
            break;
        }
    }

    ExFreePoolWithTag(Modules, 'mSdW');
    return Base;
}

static
PVOID
LoadWatchdog(VOID)
{
    SYSTEM_GDI_DRIVER_INFORMATION Information;
    NTSTATUS Status;

    RtlZeroMemory(&Information, sizeof(Information));
    RtlInitUnicodeString(&Information.DriverName, L"\\SystemRoot\\System32\\drivers\\watchdog.sys");
    Status = ZwSetSystemInformation(SystemLoadGdiDriverInSystemSpace, &Information, sizeof(Information));
    ok_eq_hex(Status, STATUS_SUCCESS);
    return NT_SUCCESS(Status) ? Information.ImageAddress : NULL;
}

START_TEST(WdSessionManager)
{
    PFN_SMGR_REGISTER Register;
    PFN_SMGR_UNREGISTER Unregister;
    PFN_SMGR_NOTIFY Notify;
    NTSTATUS Status;
    PVOID Watchdog;

    Watchdog = FindWatchdog();
    if (Watchdog == NULL)
        Watchdog = LoadWatchdog();
    if (skip(Watchdog != NULL, "watchdog.sys is not loaded\n"))
        return;

    Register = (PFN_SMGR_REGISTER)RtlFindExportedRoutineByName(Watchdog, "SMgrRegisterSessionChangeCallout");
    Unregister = (PFN_SMGR_UNREGISTER)RtlFindExportedRoutineByName(Watchdog, "SMgrUnregisterSessionChangeCallout");
    Notify = (PFN_SMGR_NOTIFY)RtlFindExportedRoutineByName(Watchdog, "SMgrNotifySessionChange");
    ok(Register != NULL, "SMgrRegisterSessionChangeCallout is not exported\n");
    ok(Unregister != NULL, "SMgrUnregisterSessionChangeCallout is not exported\n");
    ok(Notify != NULL, "SMgrNotifySessionChange is not exported\n");
    ok(RtlFindExportedRoutineByName(Watchdog, "SMgrGdiCallout") != NULL, "SMgrGdiCallout is not exported\n");
    if (Register == NULL || Unregister == NULL || Notify == NULL)
        return;

    Status = Register((PVOID)TestSessionCallout, NULL, 0x9e8, RegistrationData, 0x100);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    CalloutCount = 0;
    Status = Notify(NULL, NULL, NULL, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(CalloutCount, 1L);
    ok_eq_pointer(CalloutParameters[0], NULL);
    ok_eq_pointer(CalloutParameters[2], NULL);
    ok_eq_pointer(CalloutParameters[3], (PVOID)(ULONG_PTR)1);

    Status = Unregister((PVOID)TestSessionCallout);
    ok_eq_hex(Status, STATUS_SUCCESS);

    CalloutCount = 0;
    Status = Notify(NULL, NULL, NULL, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(CalloutCount, 0L);
}
