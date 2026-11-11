/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Browser broker job lifetime, process-count and notification semantics
 */

#include "precomp.h"

#define JOB_COMPLETION_KEY ((ULONG_PTR)0x5342584a)
#define JOB_TERMINATE_STATUS 0x5a
#define JOB_SPIN_USER_TIME (50 * 10000)

static LPPROC_THREAD_ATTRIBUTE_LIST
CreateJobAttributeList(PHANDLE Job)
{
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    SIZE_T Size = 0;
    BOOL Initialized;

    InitializeProcThreadAttributeList(NULL, 1, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Attributes)
        return NULL;
    Initialized = InitializeProcThreadAttributeList(Attributes, 1, 0, &Size);
    if (!Initialized ||
        !UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                   Job, sizeof(*Job), NULL, NULL))
    {
        if (Initialized) DeleteProcThreadAttributeList(Attributes);
        HeapFree(GetProcessHeap(), 0, Attributes);
        return NULL;
    }
    return Attributes;
}

static void
FreeJobAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST Attributes)
{
    if (!Attributes) return;
    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);
}

static BOOL
SpawnInJob(HANDLE Job, PCSTR Mode, DWORD Flags, PPROCESS_INFORMATION Info)
{
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    BOOL Result;

    Attributes = CreateJobAttributeList(&Job);
    if (!Attributes)
        return FALSE;
    Result = SbxSpawnChild("JobLifecycle", Mode, NULL, NULL, Attributes,
                           Flags, Info);
    FreeJobAttributeList(Attributes);
    return Result;
}

static void
CloseProcessInformation(PPROCESS_INFORMATION Info)
{
    if (Info->hThread) CloseHandle(Info->hThread);
    if (Info->hProcess) CloseHandle(Info->hProcess);
    ZeroMemory(Info, sizeof(*Info));
}

static void
TestActiveProcessLimit(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION Accounting;
    PROCESS_INFORMATION First, Second;
    HANDLE Job;
    DWORD Error, Length;
    BOOL Result, InJob = FALSE;

    ZeroMemory(&First, sizeof(First));
    ZeroMemory(&Second, sizeof(Second));
    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObject failed %lu\n", GetLastError());
    if (!Job) return;
    ZeroMemory(&Limits, sizeof(Limits));
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    Limits.BasicLimitInformation.ActiveProcessLimit = 1;
    ok(SetInformationJobObject(Job, JobObjectExtendedLimitInformation,
                               &Limits, sizeof(Limits)),
       "active-process limit failed %lu\n", GetLastError());

    ok(SpawnInJob(Job, "wait", CREATE_SUSPENDED, &First),
       "first process launch failed %lu\n", GetLastError());
    if (!First.hProcess) goto Cleanup;
    ok(IsProcessInJob(First.hProcess, Job, &InJob) && InJob,
       "first process was not assigned at creation (%lu)\n", GetLastError());

    SetLastError(0xdeadbeef);
    Result = SpawnInJob(Job, "noop", CREATE_SUSPENDED, &Second);
    Error = GetLastError();
    ok(!Result, "active-process limit allowed a second target\n");
    if (Result)
    {
        TerminateProcess(Second.hProcess, 0);
        CloseProcessInformation(&Second);
    }
    else
    {
        ok(Error == ERROR_NOT_ENOUGH_QUOTA,
           "active-process rejection error %lu\n", Error);
    }

    ZeroMemory(&Accounting, sizeof(Accounting));
    ok(QueryInformationJobObject(Job, JobObjectBasicAccountingInformation,
                                 &Accounting, sizeof(Accounting), &Length),
       "job accounting query failed %lu\n", GetLastError());
    ok(Accounting.ActiveProcesses == 1,
       "active process count %lu\n", Accounting.ActiveProcesses);
    ok(Accounting.TotalProcesses == 1 && Accounting.TotalTerminatedProcesses == 0,
       "process totals %lu/%lu\n", Accounting.TotalProcesses,
       Accounting.TotalTerminatedProcesses);

Cleanup:
    if (First.hProcess)
    {
        TerminateProcess(First.hProcess, 0);
        WaitForSingleObject(First.hProcess, 5000);
        CloseProcessInformation(&First);
    }
    CloseHandle(Job);
}

static void
TestTerminateJob(void)
{
    PROCESS_INFORMATION Info;
    HANDLE Job;
    BOOL InJob = FALSE;
    DWORD ExitCode = STILL_ACTIVE;

    ZeroMemory(&Info, sizeof(Info));
    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObject failed %lu\n", GetLastError());
    if (!Job) return;
    ok(SpawnInJob(Job, "wait", 0, &Info),
       "terminate-job target launch failed %lu\n", GetLastError());
    if (!Info.hProcess) goto Cleanup;
    ok(IsProcessInJob(Info.hProcess, Job, &InJob) && InJob,
       "terminate-job target is not in its job\n");
    ok(TerminateJobObject(Job, JOB_TERMINATE_STATUS),
       "TerminateJobObject failed %lu\n", GetLastError());
    ok(WaitForSingleObject(Info.hProcess, 5000) == WAIT_OBJECT_0,
       "TerminateJobObject did not stop the target\n");
    ok(GetExitCodeProcess(Info.hProcess, &ExitCode) &&
       ExitCode == JOB_TERMINATE_STATUS,
       "terminated target exit code 0x%lx\n", ExitCode);

Cleanup:
    if (Info.hProcess)
    {
        if (WaitForSingleObject(Info.hProcess, 0) != WAIT_OBJECT_0)
            TerminateProcess(Info.hProcess, 0);
        CloseProcessInformation(&Info);
    }
    CloseHandle(Job);
}

static void
TestKillOnJobClose(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits;
    PROCESS_INFORMATION Info;
    HANDLE Job;
    BOOL InJob = FALSE;
    DWORD ExitCode = STILL_ACTIVE;

    ZeroMemory(&Info, sizeof(Info));
    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObject failed %lu\n", GetLastError());
    if (!Job) return;
    ZeroMemory(&Limits, sizeof(Limits));
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    ok(SetInformationJobObject(Job, JobObjectExtendedLimitInformation,
                               &Limits, sizeof(Limits)),
       "kill-on-close setup failed %lu\n", GetLastError());
    ok(SpawnInJob(Job, "wait", CREATE_SUSPENDED, &Info),
       "kill-on-close target launch failed %lu\n", GetLastError());
    if (!Info.hProcess)
    {
        CloseHandle(Job);
        return;
    }
    ok(IsProcessInJob(Info.hProcess, Job, &InJob) && InJob,
       "kill-on-close target is not in its job\n");

    CloseHandle(Job);
    Job = NULL;
    ok(WaitForSingleObject(Info.hProcess, 5000) == WAIT_OBJECT_0,
       "closing the last kill-on-close job handle did not stop the target\n");
    ok(GetExitCodeProcess(Info.hProcess, &ExitCode) && ExitCode != STILL_ACTIVE,
       "kill-on-close target remained active\n");
    CloseProcessInformation(&Info);
}

static void
TestCompletionPortLifecycle(void)
{
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT Association;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION Accounting;
    PROCESS_INFORMATION Info;
    HANDLE Job, Port;
    DWORD ExitCode, Message, Length;
    ULONG_PTR Key;
    LPOVERLAPPED Value;
    BOOL SawNew = FALSE, SawExit = FALSE, SawZero = FALSE;
    ULONG Attempt;

    Job = CreateJobObjectW(NULL, NULL);
    Port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    ZeroMemory(&Info, sizeof(Info));
    ok(Job != NULL && Port != NULL, "job/completion port creation failed %lu\n",
       GetLastError());
    if (!Job || !Port) goto Cleanup;
    Association.CompletionKey = (PVOID)JOB_COMPLETION_KEY;
    Association.CompletionPort = Port;
    ok(SetInformationJobObject(Job, JobObjectAssociateCompletionPortInformation,
                               &Association, sizeof(Association)),
       "job completion-port association failed %lu\n", GetLastError());

    ok(SpawnInJob(Job, "noop", 0, &Info),
       "completion target launch failed %lu\n", GetLastError());
    if (!Info.hProcess) goto Cleanup;
    ExitCode = SbxWaitChild(&Info);
    ok(ExitCode == 0, "completion target failed with 0x%lx\n", ExitCode);

    for (Attempt = 0; Attempt < 16 && !SawZero; Attempt++)
    {
        Key = 0;
        Message = 0;
        Value = NULL;
        if (!GetQueuedCompletionStatus(Port, &Message, &Key, &Value, 1000))
            continue;
        ok(Key == JOB_COMPLETION_KEY, "completion key %Ix\n", Key);
        if (Message == JOB_OBJECT_MSG_NEW_PROCESS)
            SawNew = TRUE;
        else if (Message == JOB_OBJECT_MSG_EXIT_PROCESS)
            SawExit = TRUE;
        else if (Message == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO)
            SawZero = TRUE;
    }
    ok(SawNew, "JOB_OBJECT_MSG_NEW_PROCESS was not delivered\n");
    ok(SawExit, "JOB_OBJECT_MSG_EXIT_PROCESS was not delivered\n");
    ok(SawZero, "JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO was not delivered\n");

    ZeroMemory(&Accounting, sizeof(Accounting));
    ok(QueryInformationJobObject(Job, JobObjectBasicAccountingInformation,
                                 &Accounting, sizeof(Accounting), &Length),
       "final accounting query failed %lu\n", GetLastError());
    ok(Accounting.TotalProcesses == 1 && Accounting.ActiveProcesses == 0 &&
       Accounting.TotalTerminatedProcesses == 0,
       "final process totals %lu/%lu/%lu\n", Accounting.TotalProcesses,
       Accounting.ActiveProcesses, Accounting.TotalTerminatedProcesses);

Cleanup:
    if (Port) CloseHandle(Port);
    if (Job) CloseHandle(Job);
}

static void
TestEndOfJobTime(void)
{
    ULONG Storage[3];
    PJOBOBJECT_END_OF_JOB_TIME_INFORMATION EndOfJob;
    DWORD Length = 0;
    HANDLE Job;

    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObject failed %lu\n", GetLastError());
    if (!Job) return;

    /* On a 4-byte boundary that is not an 8-byte one */
    EndOfJob = (PJOBOBJECT_END_OF_JOB_TIME_INFORMATION)(((ULONG_PTR)Storage % 8) ? &Storage[0] : &Storage[1]);
    EndOfJob->EndOfJobTimeAction = JOB_OBJECT_POST_AT_END_OF_JOB;
    ok(SetInformationJobObject(Job, JobObjectEndOfJobTimeInformation, EndOfJob, sizeof(*EndOfJob)),
       "end-of-job time on a 4-byte boundary failed %lu\n", GetLastError());

    EndOfJob->EndOfJobTimeAction = 0xdead;
    ok(QueryInformationJobObject(Job, JobObjectEndOfJobTimeInformation, EndOfJob, sizeof(*EndOfJob), &Length) &&
       Length == sizeof(*EndOfJob) && EndOfJob->EndOfJobTimeAction == JOB_OBJECT_POST_AT_END_OF_JOB,
       "end-of-job time not stored: error %lu, length %lu, action %lu\n",
       GetLastError(), Length, EndOfJob->EndOfJobTimeAction);

    CloseHandle(Job);
}

static void
CheckUiRestrictionChain(HANDLE Outer, HANDLE Restricted, HANDLE Plain)
{
    JOBOBJECT_BASIC_UI_RESTRICTIONS Ui = { JOB_OBJECT_UILIMIT_HANDLES };
    PROCESS_INFORMATION Info;

    ok(SetInformationJobObject(Outer, JobObjectBasicUIRestrictions, &Ui, sizeof(Ui)) &&
       SetInformationJobObject(Restricted, JobObjectBasicUIRestrictions, &Ui, sizeof(Ui)),
       "UI restrictions failed %lu\n", GetLastError());

    ok(SbxSpawnChild("JobLifecycle", "noop", NULL, NULL, NULL, CREATE_SUSPENDED, &Info),
       "child spawn failed %lu\n", GetLastError());
    if (!Info.hProcess) return;

    ok(AssignProcessToJobObject(Outer, Info.hProcess), "outer assignment failed %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(!AssignProcessToJobObject(Restricted, Info.hProcess) && GetLastError() == ERROR_NOT_SUPPORTED,
       "second UI-restricted job on the chain: %lu\n", GetLastError());

    ok(AssignProcessToJobObject(Plain, Info.hProcess), "unrestricted nested job refused %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(!SetInformationJobObject(Plain, JobObjectBasicUIRestrictions, &Ui, sizeof(Ui)) &&
       GetLastError() == ERROR_NOT_SUPPORTED,
       "UI restrictions under a restricted job: %lu\n", GetLastError());

    TerminateProcess(Info.hProcess, 0);
    CloseProcessInformation(&Info);
}

static ULONGLONG
ProcessUserTime(HANDLE Process)
{
    FILETIME Creation, Exit, Kernel, User;
    ULARGE_INTEGER Value;

    if (!GetProcessTimes(Process, &Creation, &Exit, &Kernel, &User)) return 0;
    Value.LowPart = User.dwLowDateTime;
    Value.HighPart = User.dwHighDateTime;
    return Value.QuadPart;
}

static void
SpinUserTime(void)
{
    ULONGLONG Start = GetTickCount64();
    volatile ULONG Sink = 0;
    ULONG Index;

    while (ProcessUserTime(GetCurrentProcess()) < JOB_SPIN_USER_TIME &&
           GetTickCount64() - Start < 2000)
    {
        for (Index = 0; Index < 100000; Index++) Sink += Index;
    }
}

static BOOL
JobUserTimeMatches(ULONGLONG JobTime, ULONGLONG ProcessTime)
{
    return JobTime >= ProcessTime / 2 && JobTime <= ProcessTime * 2;
}

static void
TestJobTimes(void)
{
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION Accounting;
    PROCESS_INFORMATION Info;
    ULONGLONG ChildUser = 0;
    ULONGLONG Start;
    HANDLE Job;

    ZeroMemory(&Info, sizeof(Info));
    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObject failed %lu\n", GetLastError());
    if (!Job) return;

    ok(SpawnInJob(Job, "spin", 0, &Info), "spinning process launch failed %lu\n", GetLastError());
    if (Info.hProcess)
    {
        Start = GetTickCount64();
        while ((ChildUser = ProcessUserTime(Info.hProcess)) < JOB_SPIN_USER_TIME &&
               GetTickCount64() - Start < 3000)
        {
            Sleep(10);
        }
        ok(ChildUser >= JOB_SPIN_USER_TIME, "child used %I64u of user time\n", ChildUser);

        ZeroMemory(&Accounting, sizeof(Accounting));
        ok(QueryInformationJobObject(Job, JobObjectBasicAccountingInformation,
                                     &Accounting, sizeof(Accounting), NULL),
           "job accounting query failed %lu\n", GetLastError());
        ok(JobUserTimeMatches(Accounting.TotalUserTime.QuadPart, ChildUser),
           "running process: job user time %I64d, process %I64u\n",
           Accounting.TotalUserTime.QuadPart, ChildUser);

        TerminateProcess(Info.hProcess, 0);
        ok(WaitForSingleObject(Info.hProcess, 5000) == WAIT_OBJECT_0, "spinning process did not end\n");
        ChildUser = ProcessUserTime(Info.hProcess);

        ZeroMemory(&Accounting, sizeof(Accounting));
        ok(QueryInformationJobObject(Job, JobObjectBasicAccountingInformation,
                                     &Accounting, sizeof(Accounting), NULL),
           "job accounting query failed %lu\n", GetLastError());
        ok(JobUserTimeMatches(Accounting.TotalUserTime.QuadPart, ChildUser),
           "exited process: job user time %I64d, process %I64u\n",
           Accounting.TotalUserTime.QuadPart, ChildUser);
        ok(Accounting.ThisPeriodTotalUserTime.QuadPart == Accounting.TotalUserTime.QuadPart,
           "period user time %I64d, total %I64d\n",
           Accounting.ThisPeriodTotalUserTime.QuadPart, Accounting.TotalUserTime.QuadPart);

        CloseProcessInformation(&Info);
    }

    CloseHandle(Job);
}

static HANDLE
ImpersonationWithPrivilege(PCWSTR Privilege, BOOL Enable)
{
    TOKEN_PRIVILEGES Privileges;
    HANDLE Process = NULL, Token = NULL;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &Process)) return NULL;
    if (!DuplicateTokenEx(Process, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation,
                          TokenImpersonation, &Token))
    {
        CloseHandle(Process);
        return NULL;
    }
    CloseHandle(Process);

    Privileges.PrivilegeCount = 1;
    Privileges.Privileges[0].Attributes = Enable ? SE_PRIVILEGE_ENABLED : 0;
    if (!LookupPrivilegeValueW(NULL, Privilege, &Privileges.Privileges[0].Luid) ||
        !AdjustTokenPrivileges(Token, FALSE, &Privileges, 0, NULL, NULL) ||
        GetLastError() != ERROR_SUCCESS)
    {
        CloseHandle(Token);
        return NULL;
    }
    return Token;
}

static DWORD
SetSchedulingClass(HANDLE Job, HANDLE Token, DWORD SchedulingClass)
{
    JOBOBJECT_BASIC_LIMIT_INFORMATION Limits;
    DWORD Error = ERROR_SUCCESS;

    ZeroMemory(&Limits, sizeof(Limits));
    Limits.LimitFlags = JOB_OBJECT_LIMIT_SCHEDULING_CLASS;
    Limits.SchedulingClass = SchedulingClass;

    if (!SetThreadToken(NULL, Token)) return GetLastError();
    if (!SetInformationJobObject(Job, JobObjectBasicLimitInformation, &Limits, sizeof(Limits)))
        Error = GetLastError();
    RevertToSelf();
    return Error;
}

static void
TestSchedulingClassPrivilege(void)
{
    HANDLE Job, Without, With;

    Job = CreateJobObjectW(NULL, NULL);
    Without = ImpersonationWithPrivilege(L"SeIncreaseBasePriorityPrivilege", FALSE);
    With = ImpersonationWithPrivilege(L"SeIncreaseBasePriorityPrivilege", TRUE);
    ok(Job && Without && With, "setup failed %lu\n", GetLastError());

    if (Job && Without && With)
    {
        ok_long(SetSchedulingClass(Job, Without, 5), ERROR_SUCCESS);
        ok_long(SetSchedulingClass(Job, Without, 6), ERROR_PRIVILEGE_NOT_HELD);
        ok_long(SetSchedulingClass(Job, With, 9), ERROR_SUCCESS);
    }

    if (With) CloseHandle(With);
    if (Without) CloseHandle(Without);
    if (Job) CloseHandle(Job);
}

static void
TestUiRestrictionChain(void)
{
    HANDLE Outer = CreateJobObjectW(NULL, NULL);
    HANDLE Restricted = CreateJobObjectW(NULL, NULL);
    HANDLE Plain = CreateJobObjectW(NULL, NULL);

    ok(Outer && Restricted && Plain, "CreateJobObject failed %lu\n", GetLastError());
    if (Outer && Restricted && Plain) CheckUiRestrictionChain(Outer, Restricted, Plain);

    if (Plain) CloseHandle(Plain);
    if (Restricted) CloseHandle(Restricted);
    if (Outer) CloseHandle(Outer);
}

START_TEST(JobLifecycle)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);

    if (SbxIsChild(Arguments, Count, "noop")) ExitProcess(0);
    if (SbxIsChild(Arguments, Count, "wait"))
    {
        Sleep(60000);
        ExitProcess(0);
    }
    if (SbxIsChild(Arguments, Count, "spin"))
    {
        SpinUserTime();
        Sleep(60000);
        ExitProcess(0);
    }

    TestActiveProcessLimit();
    TestTerminateJob();
    TestKillOnJobClose();
    TestCompletionPortLifecycle();
    TestEndOfJobTime();
    TestUiRestrictionChain();
    TestJobTimes();
    TestSchedulingClassPrivilege();
}
