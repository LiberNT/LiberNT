/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test SetFileIoOverlappedRange
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <winsvc.h>
#include "../advapi32/svchlp.h"

#define TEST_SE_LOCK_MEMORY_PRIVILEGE 4
#define TEST_FILE_SIZE 0x2000
#define TEST_RANGE_SIZE 0x2000

typedef BOOL (WINAPI *PFN_SET_FILE_IO_OVERLAPPED_RANGE)(HANDLE, PUCHAR, ULONG);

static PFN_SET_FILE_IO_OVERLAPPED_RANGE pSetFileIoOverlappedRange;
static SERVICE_STATUS_HANDLE ServiceStatusHandle;

static
BOOL
CreateDataFile(
    _In_ PCWSTR Path,
    _In_ PUCHAR Data)
{
    HANDLE File;
    DWORD Written = 0;
    BOOL Result;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    memset(Data, 'o', TEST_FILE_SIZE);
    Result = WriteFile(File, Data, TEST_FILE_SIZE, &Written, NULL) && Written == TEST_FILE_SIZE;
    CloseHandle(File);
    return Result;
}

static
VOID
ServiceOverlappedRead(
    _In_ HANDLE File,
    _In_ LPOVERLAPPED Overlapped,
    _In_ PUCHAR Buffer)
{
    DWORD Read = 0;
    DWORD Wait;
    BOOL Result;

    ZeroMemory(Overlapped, sizeof(*Overlapped));
    Overlapped->hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    service_ok(Overlapped->hEvent != NULL, "CreateEvent failed with %lu\n", GetLastError());
    if (!Overlapped->hEvent)
        return;

    memset(Buffer, 0, TEST_FILE_SIZE);
    Result = ReadFile(File, Buffer, TEST_FILE_SIZE, NULL, Overlapped);
    service_ok(Result || GetLastError() == ERROR_IO_PENDING, "Overlapped ReadFile failed with %lu\n", GetLastError());
    Wait = WaitForSingleObject(Overlapped->hEvent, 5000);
    service_ok(Wait == WAIT_OBJECT_0, "Waiting for the read returned %lu\n", Wait);
    if (Wait == WAIT_OBJECT_0)
    {
        Result = GetOverlappedResult(File, Overlapped, &Read, FALSE);
        service_ok(Result, "GetOverlappedResult failed with %lu\n", GetLastError());
        service_ok(Read == TEST_FILE_SIZE, "Read %lu bytes\n", Read);
        service_ok(Overlapped->Internal == STATUS_SUCCESS, "Status block 0x%Ix\n", Overlapped->Internal);
        service_ok(Overlapped->InternalHigh == TEST_FILE_SIZE, "Status block information %Iu\n", Overlapped->InternalHigh);
        service_ok(Buffer[0] == 'o' && Buffer[TEST_FILE_SIZE - 1] == 'o', "Wrong data read\n");
    }
    CloseHandle(Overlapped->hEvent);
}

static
VOID
ServiceChecks(VOID)
{
    WCHAR TempPath[MAX_PATH];
    WCHAR Path[MAX_PATH];
    PUCHAR Range;
    PUCHAR Reserved;
    PUCHAR Data;
    BOOLEAN Enabled;
    NTSTATUS Status;
    HANDLE File;
    BOOL Result;

    pSetFileIoOverlappedRange = (PFN_SET_FILE_IO_OVERLAPPED_RANGE)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetFileIoOverlappedRange");
    service_ok(pSetFileIoOverlappedRange != NULL, "kernel32 does not export SetFileIoOverlappedRange\n");
    if (!pSetFileIoOverlappedRange)
        return;

    Status = RtlAdjustPrivilege(TEST_SE_LOCK_MEMORY_PRIVILEGE, TRUE, FALSE, &Enabled);
    service_ok(Status == STATUS_SUCCESS, "Enabling SeLockMemoryPrivilege as LocalSystem failed with 0x%lx\n", Status);

    GetTempPathW(RTL_NUMBER_OF(TempPath), TempPath);
    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%sovrsvc%lx.bin", TempPath, GetCurrentProcessId());
    Data = HeapAlloc(GetProcessHeap(), 0, TEST_FILE_SIZE);
    Range = VirtualAlloc(NULL, TEST_RANGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Reserved = VirtualAlloc(NULL, 0x1000, MEM_RESERVE, PAGE_READWRITE);
    service_ok(Data && Range && Reserved, "Allocation failed\n");
    if (!Data || !Range || !Reserved)
        return;

    Result = CreateDataFile(Path, Data);
    service_ok(Result, "Creating %S failed with %lu\n", Path, GetLastError());
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    service_ok(File != INVALID_HANDLE_VALUE, "Opening %S failed with %lu\n", Path, GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        SetLastError(0xdeadbeef);
        Result = pSetFileIoOverlappedRange(File, Range, 0);
        service_ok(!Result && GetLastError() == ERROR_INVALID_PARAMETER,
                   "Empty range returned %d with %lu\n", Result, GetLastError());

        SetLastError(0xdeadbeef);
        Result = pSetFileIoOverlappedRange(File, Reserved, 0x1000);
        service_ok(!Result && GetLastError() == ERROR_NOACCESS,
                   "Reserved memory returned %d with %lu\n", Result, GetLastError());

        Result = pSetFileIoOverlappedRange(File, Range, TEST_RANGE_SIZE);
        service_ok(Result, "SetFileIoOverlappedRange failed with %lu\n", GetLastError());

        SetLastError(0xdeadbeef);
        Result = pSetFileIoOverlappedRange(File, Range, 0x1000);
        service_ok(!Result && GetLastError() == ERROR_INVALID_PARAMETER,
                   "A second range returned %d with %lu\n", Result, GetLastError());

        ServiceOverlappedRead(File, (LPOVERLAPPED)(Range + 0x100), Data);
        service_ok(CloseHandle(File), "CloseHandle failed with %lu\n", GetLastError());
    }

    service_ok(VirtualFree(Range, 0, MEM_RELEASE), "VirtualFree of the range failed with %lu\n", GetLastError());
    VirtualFree(Reserved, 0, MEM_RELEASE);
    HeapFree(GetProcessHeap(), 0, Data);
    service_ok(DeleteFileW(Path), "DeleteFile(%S) failed with %lu\n", Path, GetLastError());
}

static
VOID
WINAPI
ServiceHandler(
    _In_ DWORD Control)
{
    UNREFERENCED_PARAMETER(Control);
}

static
VOID
ReportServiceStatus(
    _In_ DWORD State)
{
    SERVICE_STATUS Status = { SERVICE_WIN32_OWN_PROCESS, State, 0, NO_ERROR, 0, 0, 0 };

    service_ok(SetServiceStatus(ServiceStatusHandle, &Status), "SetServiceStatus(%lu) failed with %lu\n",
               State, GetLastError());
}

static
VOID
WINAPI
ServiceMain(
    _In_ DWORD Argc,
    _In_ LPWSTR *Argv)
{
    UNREFERENCED_PARAMETER(Argc);

    ServiceStatusHandle = RegisterServiceCtrlHandlerW(Argv[0], ServiceHandler);
    service_ok(ServiceStatusHandle != NULL, "RegisterServiceCtrlHandlerW failed with %lu\n", GetLastError());
    if (!ServiceStatusHandle)
        return;

    ReportServiceStatus(SERVICE_RUNNING);
    ServiceChecks();
    ReportServiceStatus(SERVICE_STOPPED);
}

static
BOOL
StartTestService(
    _In_ PCSTR NameA,
    _In_ PCWSTR NameW)
{
    SERVICE_TABLE_ENTRYW Table[] = { { (PWSTR)NameW, ServiceMain }, { NULL, NULL } };
    BOOL Result;

    UNREFERENCED_PARAMETER(NameA);
    Result = StartServiceCtrlDispatcherW(Table);
    service_ok(Result, "StartServiceCtrlDispatcherW failed with %lu\n", GetLastError());
    return Result;
}

static
VOID
RunService(
    _In_ PCSTR NameA,
    _In_ PCWSTR NameW,
    _In_opt_ void *Param)
{
    SERVICE_STATUS Status = { 0 };
    SC_HANDLE Manager;
    SC_HANDLE Service;
    ULONG Wait;

    UNREFERENCED_PARAMETER(NameA);
    UNREFERENCED_PARAMETER(Param);

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    ok(Manager != NULL, "OpenSCManagerW failed with %lu\n", GetLastError());
    if (!Manager)
        return;

    Service = register_service_exW(Manager, L"FileIoOverlappedRange", NameW, NULL, SERVICE_ALL_ACCESS,
                                   SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                                   NULL, NULL, NULL, NULL, NULL);
    if (Service)
    {
        ok(StartServiceW(Service, 0, NULL), "StartServiceW failed with %lu\n", GetLastError());
        for (Wait = 0; Wait < 100; Wait++)
        {
            if (!QueryServiceStatus(Service, &Status) || Status.dwCurrentState == SERVICE_STOPPED)
                break;
            Sleep(100);
        }
        ok(Status.dwCurrentState == SERVICE_STOPPED, "Service state %lu\n", Status.dwCurrentState);
        ok(DeleteService(Service), "DeleteService failed with %lu\n", GetLastError());
        CloseServiceHandle(Service);
    }
    CloseServiceHandle(Manager);
}

START_TEST(FileIoOverlappedRange)
{
    WCHAR TempPath[MAX_PATH];
    WCHAR Path[MAX_PATH];
    HMODULE KernelBase;
    HANDLE File;
    HANDLE Restricted = NULL;
    PUCHAR Range;
    PUCHAR Data;
    BOOLEAN Ignored;
    BOOL Result;
    char **Argv;
    int Argc;

    Argc = winetest_get_mainargs(&Argv);
    if (Argc >= 3)
    {
        service_process(StartTestService, Argc, Argv);
        return;
    }

    pSetFileIoOverlappedRange = (PFN_SET_FILE_IO_OVERLAPPED_RANGE)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetFileIoOverlappedRange");
    ok(pSetFileIoOverlappedRange != NULL, "kernel32 does not export SetFileIoOverlappedRange\n");
    KernelBase = GetModuleHandleW(L"kernelbase.dll");
    ok(KernelBase && GetProcAddress(KernelBase, "SetFileIoOverlappedRange") != NULL,
       "kernelbase does not export SetFileIoOverlappedRange\n");
    if (!pSetFileIoOverlappedRange)
        return;

    GetTempPathW(RTL_NUMBER_OF(TempPath), TempPath);
    StringCchPrintfW(Path, RTL_NUMBER_OF(Path), L"%sovrange%lx.bin", TempPath, GetCurrentProcessId());
    Data = HeapAlloc(GetProcessHeap(), 0, TEST_FILE_SIZE);
    Range = VirtualAlloc(NULL, TEST_RANGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    ok(Data && Range, "Allocation failed\n");
    if (!Data || !Range)
        return;
    ok(CreateDataFile(Path, Data), "Creating %S failed with %lu\n", Path, GetLastError());

    RtlAdjustPrivilege(TEST_SE_LOCK_MEMORY_PRIVILEGE, FALSE, FALSE, &Ignored);
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Opening %S failed with %lu\n", Path, GetLastError());
    if (File != INVALID_HANDLE_VALUE)
    {
        SetLastError(0xdeadbeef);
        Result = pSetFileIoOverlappedRange(File, Range, 0x1000);
        ok(!Result, "SetFileIoOverlappedRange without SeLockMemoryPrivilege succeeded\n");
        ok(GetLastError() == ERROR_PRIVILEGE_NOT_HELD,
           "SetFileIoOverlappedRange without SeLockMemoryPrivilege failed with %lu\n", GetLastError());

        ok(DuplicateHandle(GetCurrentProcess(), File, GetCurrentProcess(), &Restricted,
                           FILE_READ_DATA | SYNCHRONIZE, FALSE, 0),
           "DuplicateHandle failed with %lu\n", GetLastError());
        if (Restricted)
        {
            SetLastError(0xdeadbeef);
            Result = pSetFileIoOverlappedRange(Restricted, Range, 0x1000);
            ok(!Result, "SetFileIoOverlappedRange without FILE_READ_ATTRIBUTES succeeded\n");
            ok(GetLastError() == ERROR_ACCESS_DENIED,
               "SetFileIoOverlappedRange without FILE_READ_ATTRIBUTES failed with %lu\n", GetLastError());
            CloseHandle(Restricted);
        }
        CloseHandle(File);
    }

    VirtualFree(Range, 0, MEM_RELEASE);
    HeapFree(GetProcessHeap(), 0, Data);
    ok(DeleteFileW(Path), "DeleteFile(%S) failed with %lu\n", Path, GetLastError());

    test_runner(RunService, NULL);
}
