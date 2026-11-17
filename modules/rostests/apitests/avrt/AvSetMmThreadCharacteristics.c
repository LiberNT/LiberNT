/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the MMCSS thread characteristics and priorities avrt sets
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <stdio.h>
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <ndk/ntndk.h>
#include <avrt.h>
#include <pseh/pseh2.h>

#define PROFILE_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile"
#define TASKS_KEY PROFILE_KEY L"\\Tasks"

static HANDLE (WINAPI *pAvSetMmThreadCharacteristicsW)(LPCWSTR, LPDWORD);
static HANDLE (WINAPI *pAvSetMmThreadCharacteristicsA)(LPCSTR, LPDWORD);
static HANDLE (WINAPI *pAvSetMmMaxThreadCharacteristicsW)(LPCWSTR, LPCWSTR, LPDWORD);
static HANDLE (WINAPI *pAvSetMmMaxThreadCharacteristicsA)(LPCSTR, LPCSTR, LPDWORD);
static BOOL (WINAPI *pAvRevertMmThreadCharacteristics)(HANDLE);
static BOOL (WINAPI *pAvSetMmThreadPriority)(HANDLE, AVRT_PRIORITY);
static BOOL (WINAPI *pAvQuerySystemResponsiveness)(HANDLE, PULONG);

typedef struct _TASK_EXPECT
{
    PCWSTR Name;
    PCWSTR Category;
    DWORD Priority;
    PCWSTR BackgroundOnly;
    DWORD BackgroundPriority;
    LONG Boost;
    LONG Floor;
} TASK_EXPECT;

static const TASK_EXPECT Tasks[] =
{
    { L"Pro Audio",             L"High",   1, L"False", 0, 24, 23 },
    { L"Audio",                 L"Medium", 6, L"True",  0, 21, 16 },
    { L"Capture",               L"Medium", 5, L"True",  0, 20, 16 },
    { L"DisplayPostProcessing", L"High",   8, L"True",  8, 24, 23 },
    { L"Distribution",          L"Medium", 4, L"True",  0, 19, 16 },
    { L"Games",                 L"Medium", 2, L"False", 0, 17, 16 },
    { L"Playback",              L"Medium", 3, L"False", 4, 18, 16 },
    { L"Window Manager",        L"Medium", 5, L"True",  0, 20, 16 },
};

static VOID RunInThread(LPTHREAD_START_ROUTINE Routine, PVOID Parameter);

static LONG
CurrentPriority(VOID)
{
    THREAD_BASIC_INFORMATION Info;
    NTSTATUS Status;

    Status = NtQueryInformationThread(GetCurrentThread(), ThreadBasicInformation, &Info, sizeof(Info), NULL);
    return NT_SUCCESS(Status) ? Info.Priority : -1;
}

static KAFFINITY
CurrentAffinity(VOID)
{
    THREAD_BASIC_INFORMATION Info;
    NTSTATUS Status;

    Status = NtQueryInformationThread(GetCurrentThread(), ThreadBasicInformation, &Info, sizeof(Info), NULL);
    return NT_SUCCESS(Status) ? Info.AffinityMask : 0;
}

static INT
ExpectedThreadPriority(LONG Priority)
{
    return (Priority - 8 == 16) ? THREAD_PRIORITY_TIME_CRITICAL : Priority - 8;
}

static HANDLE
GuardedSet(PCWSTR Name, LPDWORD Index, PNTSTATUS Exception)
{
    HANDLE Handle = NULL;

    *Exception = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Handle = pAvSetMmThreadCharacteristicsW(Name, Index);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *Exception = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Handle;
}

static HANDLE
GuardedSetA(PCSTR Name, LPDWORD Index, PNTSTATUS Exception)
{
    HANDLE Handle = NULL;

    *Exception = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Handle = pAvSetMmThreadCharacteristicsA(Name, Index);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *Exception = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Handle;
}

static HANDLE
GuardedSetMaxA(PCSTR First, PCSTR Second, LPDWORD Index, PNTSTATUS Exception)
{
    HANDLE Handle = NULL;

    *Exception = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Handle = pAvSetMmMaxThreadCharacteristicsA(First, Second, Index);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *Exception = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return Handle;
}

static VOID
TestRegistry(VOID)
{
    WCHAR Text[64];
    DWORD Size, Value;
    ULONG i;
    LONG Error;

    Size = sizeof(Value);
    Error = RegGetValueW(HKEY_LOCAL_MACHINE, PROFILE_KEY, L"SystemResponsiveness", RRF_RT_REG_DWORD, NULL, &Value, &Size);
    ok_long(Error, ERROR_SUCCESS);
    ok_long(Value, 20);
    Size = sizeof(Value);
    Error = RegGetValueW(HKEY_LOCAL_MACHINE, PROFILE_KEY, L"NetworkThrottlingIndex", RRF_RT_REG_DWORD, NULL, &Value, &Size);
    ok_long(Error, ERROR_SUCCESS);
    ok_long(Value, 10);

    for (i = 0; i < RTL_NUMBER_OF(Tasks); i++)
    {
        WCHAR Path[128];

        _snwprintf(Path, RTL_NUMBER_OF(Path), L"%ls\\%ls", TASKS_KEY, Tasks[i].Name);
        Size = sizeof(Text);
        Error = RegGetValueW(HKEY_LOCAL_MACHINE, Path, L"Scheduling Category", RRF_RT_REG_SZ, NULL, Text, &Size);
        ok(Error == ERROR_SUCCESS && !wcscmp(Text, Tasks[i].Category), "%ls: category %ld '%ls'\n", Tasks[i].Name, Error, Text);
        Size = sizeof(Text);
        Error = RegGetValueW(HKEY_LOCAL_MACHINE, Path, L"Background Only", RRF_RT_REG_SZ, NULL, Text, &Size);
        ok(Error == ERROR_SUCCESS && !wcscmp(Text, Tasks[i].BackgroundOnly), "%ls: background only %ld '%ls'\n", Tasks[i].Name, Error, Text);
        Size = sizeof(Text);
        Error = RegGetValueW(HKEY_LOCAL_MACHINE, Path, L"SFIO Priority", RRF_RT_REG_SZ, NULL, Text, &Size);
        ok(Error == ERROR_SUCCESS && !wcscmp(Text, L"Normal"), "%ls: SFIO priority %ld '%ls'\n", Tasks[i].Name, Error, Text);
        Size = sizeof(Value);
        Error = RegGetValueW(HKEY_LOCAL_MACHINE, Path, L"Priority", RRF_RT_REG_DWORD, NULL, &Value, &Size);
        ok(Error == ERROR_SUCCESS && Value == Tasks[i].Priority, "%ls: priority %ld %lu\n", Tasks[i].Name, Error, Value);
        Size = sizeof(Value);
        Error = RegGetValueW(HKEY_LOCAL_MACHINE, Path, L"Affinity", RRF_RT_REG_DWORD, NULL, &Value, &Size);
        ok(Error == ERROR_SUCCESS && Value == 0, "%ls: affinity %ld 0x%lx\n", Tasks[i].Name, Error, Value);
        Size = sizeof(Value);
        Error = RegGetValueW(HKEY_LOCAL_MACHINE, Path, L"Clock Rate", RRF_RT_REG_DWORD, NULL, &Value, &Size);
        ok(Error == ERROR_SUCCESS && Value == 10000, "%ls: clock rate %ld %lu\n", Tasks[i].Name, Error, Value);
        Size = sizeof(Value);
        Error = RegGetValueW(HKEY_LOCAL_MACHINE, Path, L"GPU Priority", RRF_RT_REG_DWORD, NULL, &Value, &Size);
        ok(Error == ERROR_SUCCESS && Value == 8, "%ls: GPU priority %ld %lu\n", Tasks[i].Name, Error, Value);
        Size = sizeof(Value);
        Error = RegGetValueW(HKEY_LOCAL_MACHINE, Path, L"BackgroundPriority", RRF_RT_REG_DWORD, NULL, &Value, &Size);
        if (Tasks[i].BackgroundPriority)
            ok(Error == ERROR_SUCCESS && Value == Tasks[i].BackgroundPriority, "%ls: background priority %ld %lu\n", Tasks[i].Name, Error, Value);
        else
            ok(Error == ERROR_FILE_NOT_FOUND, "%ls: background priority %ld\n", Tasks[i].Name, Error);
    }
}

static VOID
TestErrors(VOID)
{
    NTSTATUS Exception;
    HANDLE Handle;
    DWORD Index;
    BOOL Ret;

    Index = 0;
    Handle = GuardedSet(NULL, &Index, &Exception);
    ok_hex(Exception, STATUS_ACCESS_VIOLATION);
    ok_ptr(Handle, NULL);

    Handle = GuardedSet(L"Pro Audio", NULL, &Exception);
    ok_hex(Exception, STATUS_ACCESS_VIOLATION);
    ok_ptr(Handle, NULL);

    SetLastError(0xdeadbeef);
    Index = 0;
    Handle = GuardedSetA(NULL, &Index, &Exception);
    ok_hex(Exception, STATUS_ACCESS_VIOLATION);
    ok_ptr(Handle, NULL);

    SetLastError(0xdeadbeef);
    Handle = GuardedSetA("Pro Audio", NULL, &Exception);
    ok_hex(Exception, STATUS_ACCESS_VIOLATION);
    ok_ptr(Handle, NULL);

    SetLastError(0xdeadbeef);
    Index = 0;
    Handle = GuardedSetMaxA(NULL, "Pro Audio", &Index, &Exception);
    ok_hex(Exception, STATUS_ACCESS_VIOLATION);
    ok_ptr(Handle, NULL);

    SetLastError(0xdeadbeef);
    Index = 0;
    Handle = GuardedSetMaxA("", "Pro Audio", &Index, &Exception);
    ok(Handle == NULL && Exception == STATUS_SUCCESS && GetLastError() == ERROR_INVALID_TASK_NAME,
       "max A empty first: %p exception 0x%lx error %lu\n", Handle, Exception, GetLastError());

    SetLastError(0xdeadbeef);
    Index = 0;
    Handle = pAvSetMmThreadCharacteristicsW(L"No Such Task", &Index);
    ok_ptr(Handle, NULL);
    ok_err(ERROR_INVALID_TASK_NAME);
    ok_long(Index, 0);

    SetLastError(0xdeadbeef);
    Index = 0;
    Handle = pAvSetMmThreadCharacteristicsW(L"", &Index);
    ok_ptr(Handle, NULL);
    ok_err(ERROR_INVALID_TASK_NAME);

    SetLastError(0xdeadbeef);
    Index = 12345;
    Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
    ok_ptr(Handle, NULL);
    ok_err(ERROR_INVALID_TASK_INDEX);
    ok_long(Index, 12345);
    ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_NORMAL);

    SetLastError(0xdeadbeef);
    Ret = pAvRevertMmThreadCharacteristics(NULL);
    ok_int(Ret, FALSE);
    ok_err(ERROR_INVALID_HANDLE);

    SetLastError(0xdeadbeef);
    Ret = pAvSetMmThreadPriority(NULL, AVRT_PRIORITY_HIGH);
    ok_int(Ret, FALSE);
    ok_err(ERROR_INVALID_HANDLE);

    SetLastError(0xdeadbeef);
    Ret = pAvSetMmThreadPriority(NULL, (AVRT_PRIORITY)5);
    ok_int(Ret, FALSE);
    ok_err(ERROR_INVALID_HANDLE);

    SetLastError(0xdeadbeef);
    Index = 0;
    Ret = pAvQuerySystemResponsiveness(NULL, &Index);
    ok_int(Ret, FALSE);
    ok_err(ERROR_INVALID_HANDLE);

    Handle = CreateFileW(L"\\\\.\\NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    ok(Handle != INVALID_HANDLE_VALUE, "NUL not opened: %lu\n", GetLastError());
    if (Handle != INVALID_HANDLE_VALUE)
    {
        SetLastError(0xdeadbeef);
        Ret = pAvSetMmThreadPriority(Handle, AVRT_PRIORITY_HIGH);
        ok(!Ret && GetLastError() == ERROR_INVALID_FUNCTION, "priority on another device %d %lu\n", Ret, GetLastError());
        Ret = pAvRevertMmThreadCharacteristics(Handle);
        ok(Ret, "revert on another device failed: %lu\n", GetLastError());
        SetLastError(0xdeadbeef);
        Ret = CloseHandle(Handle);
        ok(!Ret && GetLastError() == ERROR_INVALID_HANDLE, "revert left the NUL handle open: %d %lu\n", Ret, GetLastError());
    }
}

static DWORD WINAPI
TaskThread(PVOID Parameter)
{
    const TASK_EXPECT *Task = Parameter;
    HANDLE Handle, Second;
    ULONG Responsiveness;
    DWORD Index, Index2;
    LONG Level, Expected;
    BOOL Ret;

    ok_long(CurrentPriority(), 8);
    Index = 0;
    Handle = pAvSetMmThreadCharacteristicsW(Task->Name, &Index);
    ok(Handle != NULL, "%ls: not registered: %lu\n", Task->Name, GetLastError());
    if (!Handle)
        return 0;
    ok(Index != 0, "%ls: index 0\n", Task->Name);
    ok(CurrentPriority() == Task->Boost, "%ls: priority %ld, expected %ld\n", Task->Name, CurrentPriority(), Task->Boost);
    ok_int(GetThreadPriority(GetCurrentThread()), ExpectedThreadPriority(Task->Boost));
    Sleep(20);
    ok(CurrentPriority() == Task->Boost, "%ls: priority %ld after sleep, expected %ld\n", Task->Name, CurrentPriority(), Task->Boost);

    Responsiveness = 0xdeadbeef;
    Ret = pAvQuerySystemResponsiveness(Handle, &Responsiveness);
    ok_int(Ret, TRUE);
    ok_long(Responsiveness, 20);

    for (Level = -3; Level <= 3; Level++)
    {
        SetLastError(0xdeadbeef);
        Ret = pAvSetMmThreadPriority(Handle, (AVRT_PRIORITY)Level);
        if (Level < AVRT_PRIORITY_VERYLOW || Level > AVRT_PRIORITY_CRITICAL)
        {
            ok(!Ret && GetLastError() == ERROR_INVALID_PARAMETER, "%ls: level %ld ret %d error %lu\n", Task->Name, Level, Ret, GetLastError());
            continue;
        }
        Expected = max(Task->Boost + Level, Task->Floor);
        ok(Ret, "%ls: level %ld failed: %lu\n", Task->Name, Level, GetLastError());
        ok(CurrentPriority() == Expected, "%ls: level %ld priority %ld, expected %ld\n", Task->Name, Level, CurrentPriority(), Expected);
        ok(GetThreadPriority(GetCurrentThread()) == ExpectedThreadPriority(Expected), "%ls: level %ld GetThreadPriority %d\n",
           Task->Name, Level, GetThreadPriority(GetCurrentThread()));
    }
    pAvSetMmThreadPriority(Handle, AVRT_PRIORITY_NORMAL);

    Index2 = 0;
    SetLastError(0xdeadbeef);
    Second = pAvSetMmThreadCharacteristicsW(L"Games", &Index2);
    ok(Second == NULL && GetLastError() == ERROR_THREAD_ALREADY_IN_TASK, "%ls: nested %p error %lu\n", Task->Name, Second, GetLastError());
    ok(CurrentPriority() == Task->Boost, "%ls: priority %ld after nested\n", Task->Name, CurrentPriority());
    if (Second)
        pAvRevertMmThreadCharacteristics(Second);

    Ret = pAvRevertMmThreadCharacteristics(Handle);
    ok_int(Ret, TRUE);
    ok_long(CurrentPriority(), 8);
    ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_NORMAL);
    SetLastError(0xdeadbeef);
    Ret = pAvRevertMmThreadCharacteristics(Handle);
    ok(!Ret && GetLastError() == ERROR_INVALID_HANDLE, "%ls: second revert %d error %lu\n", Task->Name, Ret, GetLastError());
    SetLastError(0xdeadbeef);
    Ret = pAvSetMmThreadPriority(Handle, AVRT_PRIORITY_HIGH);
    ok(!Ret && GetLastError() == ERROR_INVALID_HANDLE, "%ls: priority after revert %d error %lu\n", Task->Name, Ret, GetLastError());
    return 0;
}

static VOID
RunInThread(LPTHREAD_START_ROUTINE Routine, PVOID Parameter)
{
    HANDLE Thread = CreateThread(NULL, 0, Routine, Parameter, 0, NULL);

    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        return;
    WaitForSingleObject(Thread, INFINITE);
    CloseHandle(Thread);
}

static DWORD WINAPI
LowercaseThread(PVOID Parameter)
{
    HANDLE Handle;
    DWORD Index = 0;

    UNREFERENCED_PARAMETER(Parameter);
    Handle = pAvSetMmThreadCharacteristicsW(L"pro audio", &Index);
    ok(Handle != NULL, "lowercase task name rejected: %lu\n", GetLastError());
    ok_long(CurrentPriority(), 24);
    if (Handle)
        pAvRevertMmThreadCharacteristics(Handle);

    Index = 0;
    Handle = pAvSetMmThreadCharacteristicsA("Audio", &Index);
    ok(Handle != NULL, "ANSI task name rejected: %lu\n", GetLastError());
    ok_long(CurrentPriority(), 21);
    if (Handle)
        pAvRevertMmThreadCharacteristics(Handle);
    return 0;
}

static DWORD WINAPI
IndexThread(PVOID Parameter)
{
    HANDLE Handle;
    DWORD Index = 0, Saved;

    UNREFERENCED_PARAMETER(Parameter);
    Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
    ok(Handle != NULL, "Pro Audio not registered: %lu\n", GetLastError());
    if (!Handle)
        return 0;
    Saved = Index;
    pAvRevertMmThreadCharacteristics(Handle);

    SetLastError(0xdeadbeef);
    Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
    ok(Handle == NULL && GetLastError() == ERROR_INVALID_TASK_INDEX, "own index reused: %p %lu\n", Handle, GetLastError());
    ok_long(Index, Saved);
    if (Handle)
        pAvRevertMmThreadCharacteristics(Handle);

    SetLastError(0xdeadbeef);
    Handle = pAvSetMmThreadCharacteristicsW(L"Audio", &Index);
    ok(Handle == NULL && GetLastError() == ERROR_INVALID_TASK_INDEX, "index reused for another task: %p %lu\n", Handle, GetLastError());
    if (Handle)
        pAvRevertMmThreadCharacteristics(Handle);
    ok_long(CurrentPriority(), 8);
    return 0;
}

static HANDLE CrossHandle;
static HANDLE CrossReady, CrossMeasure, CrossMeasured, CrossDone;
static DWORD CrossIndex;
static LONG CrossOwnerPriority;

static DWORD WINAPI
CrossOwner(PVOID Parameter)
{
    BOOL Ret;

    UNREFERENCED_PARAMETER(Parameter);
    CrossIndex = 0;
    CrossHandle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &CrossIndex);
    ok(CrossHandle != NULL, "Pro Audio not registered: %lu\n", GetLastError());
    SetEvent(CrossReady);
    WaitForSingleObject(CrossMeasure, INFINITE);
    CrossOwnerPriority = CurrentPriority();
    SetEvent(CrossMeasured);
    WaitForSingleObject(CrossDone, INFINITE);
    ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_NORMAL);
    if (CrossHandle)
    {
        SetLastError(0xdeadbeef);
        Ret = pAvRevertMmThreadCharacteristics(CrossHandle);
        ok(!Ret && GetLastError() == ERROR_INVALID_HANDLE, "owner revert after cross revert %d %lu\n", Ret, GetLastError());
    }
    return 0;
}

static DWORD WINAPI
JoinLiveIndex(PVOID Parameter)
{
    DWORD Index = CrossIndex;
    HANDLE Handle;

    UNREFERENCED_PARAMETER(Parameter);
    SetLastError(0xdeadbeef);
    Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
    ok(Handle != NULL, "live index not joined: %lu\n", GetLastError());
    ok_long(Index, CrossIndex);
    ok_long(CurrentPriority(), 24);
    if (Handle)
    {
        ok(pAvRevertMmThreadCharacteristics(Handle), "revert failed: %lu\n", GetLastError());
        ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_NORMAL);
    }

    Index = CrossIndex;
    SetLastError(0xdeadbeef);
    Handle = pAvSetMmThreadCharacteristicsW(L"Audio", &Index);
    ok(Handle == NULL && GetLastError() == ERROR_INVALID_TASK_INDEX, "live index joined another task: %p %lu\n", Handle, GetLastError());
    ok_long(Index, CrossIndex);
    ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_NORMAL);
    if (Handle)
        pAvRevertMmThreadCharacteristics(Handle);
    return 0;
}

static DWORD WINAPI
StaleIndex(PVOID Parameter)
{
    DWORD Index = CrossIndex;
    HANDLE Handle;

    UNREFERENCED_PARAMETER(Parameter);
    SetLastError(0xdeadbeef);
    Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
    ok(Handle == NULL && GetLastError() == ERROR_INVALID_TASK_INDEX, "reverted index joined: %p %lu\n", Handle, GetLastError());
    ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_NORMAL);
    if (Handle)
        pAvRevertMmThreadCharacteristics(Handle);
    return 0;
}

static VOID
TestCrossThread(VOID)
{
    HANDLE Owner;
    BOOL Ret;

    CrossReady = CreateEventW(NULL, FALSE, FALSE, NULL);
    CrossMeasure = CreateEventW(NULL, FALSE, FALSE, NULL);
    CrossMeasured = CreateEventW(NULL, FALSE, FALSE, NULL);
    CrossDone = CreateEventW(NULL, FALSE, FALSE, NULL);
    Owner = CreateThread(NULL, 0, CrossOwner, NULL, 0, NULL);
    WaitForSingleObject(CrossReady, INFINITE);
    if (CrossHandle)
    {
        RunInThread(JoinLiveIndex, NULL);
        SetLastError(0xdeadbeef);
        Ret = pAvSetMmThreadPriority(CrossHandle, AVRT_PRIORITY_HIGH);
        ok(Ret, "cross-thread priority failed: %lu\n", GetLastError());
        ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_NORMAL);
        SetEvent(CrossMeasure);
        WaitForSingleObject(CrossMeasured, INFINITE);
        ok_long(CrossOwnerPriority, 25);
        Ret = pAvRevertMmThreadCharacteristics(CrossHandle);
        ok_int(Ret, TRUE);
        RunInThread(StaleIndex, NULL);
    }
    else
    {
        SetEvent(CrossMeasure);
    }
    SetEvent(CrossDone);
    WaitForSingleObject(Owner, INFINITE);
    CloseHandle(Owner);
    CloseHandle(CrossReady);
    CloseHandle(CrossMeasure);
    CloseHandle(CrossMeasured);
    CloseHandle(CrossDone);
}

static DWORD WINAPI
MaxThread(PVOID Parameter)
{
    HANDLE Handle;
    DWORD Index = 0;

    UNREFERENCED_PARAMETER(Parameter);
    Handle = pAvSetMmMaxThreadCharacteristicsW(L"Audio", L"Pro Audio", &Index);
    ok(Handle != NULL, "Audio + Pro Audio not registered: %lu\n", GetLastError());
    ok_long(CurrentPriority(), 24);
    if (Handle)
        pAvRevertMmThreadCharacteristics(Handle);

    Index = 0;
    Handle = pAvSetMmMaxThreadCharacteristicsW(L"Pro Audio", NULL, &Index);
    ok(Handle != NULL, "Pro Audio + NULL not registered: %lu\n", GetLastError());
    ok_long(CurrentPriority(), 24);
    if (Handle)
        pAvRevertMmThreadCharacteristics(Handle);
    ok_long(CurrentPriority(), 8);
    return 0;
}

static DWORD WINAPI
HandleThread(PVOID Parameter)
{
    UCHAR Buffer[512];
    POBJECT_TYPE_INFORMATION Type = (PVOID)Buffer;
    POBJECT_NAME_INFORMATION Name = (PVOID)Buffer;
    UNICODE_STRING FileType = RTL_CONSTANT_STRING(L"File");
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Device\\MMCSS");
    OBJECT_BASIC_INFORMATION Basic;
    NTSTATUS Status;
    HANDLE Handle;
    DWORD Index = 0;

    UNREFERENCED_PARAMETER(Parameter);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
    ok(Handle != NULL, "Pro Audio not registered: %lu\n", GetLastError());
    if (!Handle)
        return 0;

    Status = NtQueryObject(Handle, ObjectTypeInformation, Buffer, sizeof(Buffer), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(RtlEqualUnicodeString(&Type->TypeName, &FileType, FALSE), "handle type %wZ\n", &Type->TypeName);
    Status = NtQueryObject(Handle, ObjectNameInformation, Buffer, sizeof(Buffer), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(RtlEqualUnicodeString(&Name->Name, &DeviceName, TRUE), "handle name %wZ\n", &Name->Name);
    Status = NtQueryObject(Handle, ObjectBasicInformation, &Basic, sizeof(Basic), NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok_hex(Basic.GrantedAccess, FILE_GENERIC_READ);

    ok_int(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST), TRUE);
    ok_long(CurrentPriority(), 6);
    Sleep(50);
    ok_long(CurrentPriority(), 6);

    ok_int(pAvRevertMmThreadCharacteristics(Handle), TRUE);
    ok_long(CurrentPriority(), 9);
    ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_ABOVE_NORMAL);
    return 0;
}

static DWORD WINAPI
AffinityThread(PVOID Parameter)
{
    const TASK_EXPECT *Task = Parameter;
    DWORD_PTR ProcessMask, SystemMask;
    HANDLE Handle;
    DWORD Index = 0;

    ok(GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask), "GetProcessAffinityMask failed: %lu\n", GetLastError());
    if ((ProcessMask & 3) != 3)
    {
        skip("%ls: moving a registered thread needs processors 0 and 1\n", Task->Name);
        return 0;
    }
    ok(SetThreadAffinityMask(GetCurrentThread(), 1) != 0, "SetThreadAffinityMask failed: %lu\n", GetLastError());
    ok(CurrentAffinity() == 1, "%ls: affinity 0x%I64x\n", Task->Name, (ULONG64)CurrentAffinity());
    Handle = pAvSetMmThreadCharacteristicsW(Task->Name, &Index);
    ok(Handle != NULL, "%ls: not registered: %lu\n", Task->Name, GetLastError());
    if (!Handle)
        return 0;
    ok(CurrentAffinity() == ProcessMask, "%ls: registered with affinity 0x%I64x, process 0x%I64x\n", Task->Name, (ULONG64)CurrentAffinity(), (ULONG64)ProcessMask);
    ok(SetThreadAffinityMask(GetCurrentThread(), 2) != 0, "SetThreadAffinityMask failed: %lu\n", GetLastError());
    ok(CurrentAffinity() == 2, "%ls: affinity 0x%I64x\n", Task->Name, (ULONG64)CurrentAffinity());
    Sleep(50);
    ok(CurrentAffinity() == 2, "%ls: affinity 0x%I64x\n", Task->Name, (ULONG64)CurrentAffinity());
    ok_int(pAvRevertMmThreadCharacteristics(Handle), TRUE);
    ok(CurrentAffinity() == 1, "%ls: affinity 0x%I64x\n", Task->Name, (ULONG64)CurrentAffinity());
    return 0;
}

static DWORD WINAPI
IdleClassThread(PVOID Parameter)
{
    HANDLE Handle;
    DWORD Index = 0;

    UNREFERENCED_PARAMETER(Parameter);
    ok_long(CurrentPriority(), 4);
    Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
    ok(Handle != NULL, "Pro Audio not registered: %lu\n", GetLastError());
    ok_long(CurrentPriority(), 24);
    if (Handle)
        pAvRevertMmThreadCharacteristics(Handle);
    ok_long(CurrentPriority(), 4);
    return 0;
}

static DWORD WINAPI
SetThreadPriorityThread(PVOID Parameter)
{
    const TASK_EXPECT *Task = Parameter;
    HANDLE Handle;
    DWORD Index = 0;

    Handle = pAvSetMmThreadCharacteristicsW(Task->Name, &Index);
    ok(Handle != NULL, "%ls: not registered: %lu\n", Task->Name, GetLastError());
    if (!Handle)
        return 0;
    ok_int(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL), TRUE);
    ok(CurrentPriority() == 15, "%ls: time critical priority %ld\n", Task->Name, CurrentPriority());
    ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_TIME_CRITICAL);
    ok_int(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST), TRUE);
    ok(CurrentPriority() == 10, "%ls: highest priority %ld\n", Task->Name, CurrentPriority());
    ok_int(pAvSetMmThreadPriority(Handle, AVRT_PRIORITY_HIGH), TRUE);
    ok(CurrentPriority() == Task->Boost + 1, "%ls: priority %ld after AVRT_PRIORITY_HIGH\n", Task->Name, CurrentPriority());
    ok_int(GetThreadPriority(GetCurrentThread()), ExpectedThreadPriority(Task->Boost + 1));
    ok_int(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL), TRUE);
    Sleep(20);
    ok(CurrentPriority() == 8, "%ls: normal priority %ld\n", Task->Name, CurrentPriority());
    ok_int(pAvSetMmThreadPriority(Handle, AVRT_PRIORITY_NORMAL), TRUE);
    ok(CurrentPriority() == Task->Boost, "%ls: priority %ld after AVRT_PRIORITY_NORMAL\n", Task->Name, CurrentPriority());
    ok_int(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL), TRUE);
    ok_int(pAvRevertMmThreadCharacteristics(Handle), TRUE);
    ok_long(CurrentPriority(), 8);
    ok_int(GetThreadPriority(GetCurrentThread()), THREAD_PRIORITY_NORMAL);
    return 0;
}

static DWORD WINAPI
RuntimeTaskThread(PVOID Parameter)
{
    HANDLE Handle;
    DWORD Index = 0;

    SetLastError(0xdeadbeef);
    Handle = pAvSetMmThreadCharacteristicsW(Parameter, &Index);
    ok(Handle == NULL && GetLastError() == ERROR_INVALID_TASK_NAME, "runtime task %ls: %p %lu\n", (PCWSTR)Parameter, Handle, GetLastError());
    if (Handle)
        pAvRevertMmThreadCharacteristics(Handle);
    return 0;
}

static VOID
TestRuntimeTask(VOID)
{
    static const WCHAR Category[] = L"Medium";
    DWORD Priority = 5;
    HKEY Key;
    LONG Error;

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, TASKS_KEY L"\\LiberNT Runtime", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &Key, NULL);
    if (Error != ERROR_SUCCESS)
    {
        skip("cannot create a task key: %ld\n", Error);
        return;
    }
    RegSetValueExW(Key, L"Scheduling Category", 0, REG_SZ, (PBYTE)Category, sizeof(Category));
    RegSetValueExW(Key, L"Priority", 0, REG_DWORD, (PBYTE)&Priority, sizeof(Priority));
    RegCloseKey(Key);
    RunInThread(RuntimeTaskThread, (PVOID)L"LiberNT Runtime");
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, TASKS_KEY L"\\LiberNT Runtime");
}

static DWORD WINAPI
TimelineThread(PVOID Parameter)
{
    LARGE_INTEGER Frequency, Start, Now, Last;
    LONG P, Prev = -1, Changes = 0;
    DWORD Index = 0;
    HANDLE Handle;

    UNREFERENCED_PARAMETER(Parameter);
    Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
    if (!Handle)
        return 0;
    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    Last = Start;
    do
    {
        QueryPerformanceCounter(&Now);
        P = CurrentPriority();
        if (P != Prev)
        {
            if (Changes < 12)
                trace("timeline %I64d us: priority %ld (previous %ld for %I64d us)\n",
                      (Now.QuadPart - Start.QuadPart) * 1000000 / Frequency.QuadPart, P, Prev,
                      (Now.QuadPart - Last.QuadPart) * 1000000 / Frequency.QuadPart);
            Changes++;
            Prev = P;
            Last = Now;
        }
    } while (Now.QuadPart - Start.QuadPart < Frequency.QuadPart);
    trace("timeline: %ld priority changes in 1 s\n", Changes);
    pAvRevertMmThreadCharacteristics(Handle);
    return 0;
}

static volatile HANDLE RaceHandle;
static volatile LONG RaceArmed, RaceStop;
static SRWLOCK RaceLock = SRWLOCK_INIT;

static DWORD WINAPI
RaceCloser(PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    while (!RaceStop)
    {
        AcquireSRWLockShared(&RaceLock);
        if (RaceArmed && RaceHandle != NULL)
            NtClose(RaceHandle);
        ReleaseSRWLockShared(&RaceLock);
    }
    return 0;
}

static DWORD WINAPI
RaceRegisterThread(PVOID Parameter)
{
    LARGE_INTEGER Frequency, Start, Now;
    ULONG Registered = 0, ClosedUnderUs = 0, Iterations = 0;
    LONG Stuck = -1;
    HANDLE Closer, Handle;
    DWORD Index;

    UNREFERENCED_PARAMETER(Parameter);
    RaceHandle = NULL;
    RaceStop = FALSE;
    Closer = CreateThread(NULL, 0, RaceCloser, NULL, 0, NULL);
    ok(Closer != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Closer)
        return 0;
    QueryPerformanceFrequency(&Frequency);
    QueryPerformanceCounter(&Start);
    do
    {
        Index = 0;
        InterlockedExchange(&RaceArmed, TRUE);
        Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
        if (Handle != NULL)
        {
            Registered++;
            RaceHandle = Handle;
            if (!pAvRevertMmThreadCharacteristics(Handle))
                ClosedUnderUs++;
        }
        InterlockedExchange(&RaceArmed, FALSE);
        AcquireSRWLockExclusive(&RaceLock);
        ReleaseSRWLockExclusive(&RaceLock);
        if (CurrentPriority() >= LOW_REALTIME_PRIORITY)
        {
            Stuck = CurrentPriority();
            break;
        }
        Iterations++;
        QueryPerformanceCounter(&Now);
    } while (Iterations < 20000 && Now.QuadPart - Start.QuadPart < Frequency.QuadPart);
    RaceStop = TRUE;
    WaitForSingleObject(Closer, INFINITE);
    CloseHandle(Closer);

    ok(Stuck == -1, "priority %ld left after iteration %lu with the handle closed\n", Stuck, Iterations);
    ok(Registered != 0, "no registration in %lu iterations\n", Iterations);
    trace("race: %lu iterations, %lu registered, %lu closed by the other thread\n", Iterations, Registered, ClosedUnderUs);
    return 0;
}

#define MANY_THREADS 128

static volatile LONG ManyPending, ManyRegistered, ManyRaised, ManyReverted, ManyError;
static HANDLE ManyAllRegistered, ManyRelease;

static DWORD WINAPI
ManyThread(PVOID Parameter)
{
    DWORD Index = 0;
    HANDLE Handle;

    UNREFERENCED_PARAMETER(Parameter);
    Handle = pAvSetMmThreadCharacteristicsW(L"Pro Audio", &Index);
    if (Handle != NULL)
        InterlockedIncrement(&ManyRegistered);
    else
        InterlockedCompareExchange(&ManyError, GetLastError(), 0);
    if (InterlockedDecrement(&ManyPending) == 0)
        SetEvent(ManyAllRegistered);
    WaitForSingleObject(ManyAllRegistered, INFINITE);
    if (Handle != NULL && CurrentPriority() == 24)
        InterlockedIncrement(&ManyRaised);
    WaitForSingleObject(ManyRelease, INFINITE);
    if (Handle != NULL && pAvRevertMmThreadCharacteristics(Handle) && CurrentPriority() < LOW_REALTIME_PRIORITY)
        InterlockedIncrement(&ManyReverted);
    return 0;
}

static VOID
TestManyThreads(VOID)
{
    HANDLE Threads[MANY_THREADS];
    ULONG i, Created = 0;

    ManyPending = MANY_THREADS;
    ManyRegistered = ManyRaised = ManyReverted = ManyError = 0;
    ManyAllRegistered = CreateEventW(NULL, TRUE, FALSE, NULL);
    ManyRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (i = 0; i < MANY_THREADS; i++)
    {
        Threads[i] = CreateThread(NULL, 0, ManyThread, NULL, 0, NULL);
        if (Threads[i] != NULL)
            Created++;
        else if (InterlockedDecrement(&ManyPending) == 0)
            SetEvent(ManyAllRegistered);
    }
    ok_long(Created, MANY_THREADS);
    ok_long(WaitForSingleObject(ManyAllRegistered, 30000), WAIT_OBJECT_0);
    SetEvent(ManyRelease);
    for (i = 0; i < MANY_THREADS; i++)
    {
        if (Threads[i] == NULL)
            continue;
        ok_long(WaitForSingleObject(Threads[i], 30000), WAIT_OBJECT_0);
        CloseHandle(Threads[i]);
    }
    CloseHandle(ManyAllRegistered);
    CloseHandle(ManyRelease);
    trace("%lu threads of one process: %ld registered, %ld at 24, %ld reverted, first error %ld\n",
          Created, ManyRegistered, ManyRaised, ManyReverted, ManyError);
    ok(ManyRegistered >= 32, "%ld of %lu threads registered\n", ManyRegistered, Created);
    if (ManyRegistered < (LONG)Created)
        ok_long(ManyError, ERROR_TOO_MANY_THREADS);
    else
        trace("no limit on the threads of one process, where Windows 11 registers 32\n");
    ok_long(ManyRaised, ManyRegistered);
    ok_long(ManyReverted, ManyRegistered);
}

START_TEST(AvSetMmThreadCharacteristics)
{
    HMODULE Avrt = LoadLibraryW(L"avrt.dll");
    ULONG i;

    ok(Avrt != NULL, "avrt.dll not loaded: %lu\n", GetLastError());
    if (!Avrt)
        return;
    pAvSetMmThreadCharacteristicsW = (PVOID)GetProcAddress(Avrt, "AvSetMmThreadCharacteristicsW");
    pAvSetMmThreadCharacteristicsA = (PVOID)GetProcAddress(Avrt, "AvSetMmThreadCharacteristicsA");
    pAvSetMmMaxThreadCharacteristicsW = (PVOID)GetProcAddress(Avrt, "AvSetMmMaxThreadCharacteristicsW");
    pAvSetMmMaxThreadCharacteristicsA = (PVOID)GetProcAddress(Avrt, "AvSetMmMaxThreadCharacteristicsA");
    pAvRevertMmThreadCharacteristics = (PVOID)GetProcAddress(Avrt, "AvRevertMmThreadCharacteristics");
    pAvSetMmThreadPriority = (PVOID)GetProcAddress(Avrt, "AvSetMmThreadPriority");
    pAvQuerySystemResponsiveness = (PVOID)GetProcAddress(Avrt, "AvQuerySystemResponsiveness");
    if (!pAvSetMmThreadCharacteristicsW || !pAvSetMmThreadCharacteristicsA || !pAvSetMmMaxThreadCharacteristicsW ||
        !pAvSetMmMaxThreadCharacteristicsA ||
        !pAvRevertMmThreadCharacteristics || !pAvSetMmThreadPriority || !pAvQuerySystemResponsiveness)
    {
        ok(0, "avrt export missing\n");
        return;
    }

    TestRegistry();
    TestErrors();
    for (i = 0; i < RTL_NUMBER_OF(Tasks); i++)
        RunInThread(TaskThread, (PVOID)&Tasks[i]);
    RunInThread(LowercaseThread, NULL);
    RunInThread(IndexThread, NULL);
    TestCrossThread();
    RunInThread(MaxThread, NULL);
    RunInThread(HandleThread, NULL);
    RunInThread(SetThreadPriorityThread, (PVOID)&Tasks[0]);
    RunInThread(SetThreadPriorityThread, (PVOID)&Tasks[1]);
    TestRuntimeTask();
    RunInThread(AffinityThread, (PVOID)&Tasks[0]);
    RunInThread(AffinityThread, (PVOID)&Tasks[1]);
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
    RunInThread(IdleClassThread, NULL);
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    RunInThread(RaceRegisterThread, NULL);
    RunInThread(TimelineThread, NULL);
    TestManyThreads();
}
