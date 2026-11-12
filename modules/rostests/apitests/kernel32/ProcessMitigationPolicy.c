/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the one-way runtime mitigation policies of SetProcessMitigationPolicy
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef BOOL (WINAPI *PFN_SET_THREAD_INFORMATION)(HANDLE, THREAD_INFORMATION_CLASS, PVOID, DWORD);

static const struct
{
    PROCESS_MITIGATION_POLICY Policy;
    PCSTR Name;
    DWORD FlagCount;
    DWORD Flags;
} Policies[] =
{
    { ProcessDynamicCodePolicy, "dynamic code", 4, 0x3 },
    { ProcessStrictHandleCheckPolicy, "strict handle check", 2, 0x3 },
    { ProcessSystemCallDisablePolicy, "system call disable", 4, 0x1 },
    { ProcessSignaturePolicy, "signature", 5, 0x1 },
    { ProcessChildProcessPolicy, "child process", 3, 0x1 },
};

static DWORD
GetPolicy(PROCESS_MITIGATION_POLICY Policy)
{
    DWORD Value = 0xdeadbeef;

    ok(GetProcessMitigationPolicy(GetCurrentProcess(), Policy, &Value, sizeof(Value)),
       "GetProcessMitigationPolicy(%d) failed %lu\n", Policy, GetLastError());
    return Value;
}

static BOOL
SetPolicy(PROCESS_MITIGATION_POLICY Policy, DWORD Value)
{
    return SetProcessMitigationPolicy(Policy, &Value, sizeof(Value));
}

static void
TestDynamicCode(void)
{
    PFN_SET_THREAD_INFORMATION pSetThreadInformation;
    DWORD Allow = 1;
    PVOID Memory;

    SetLastError(0xdeadbeef);
    Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    ok(Memory == NULL, "executable memory allocated under ProhibitDynamicCode\n");
    ok(GetLastError() == ERROR_DYNAMIC_CODE_BLOCKED, "error %lu\n", GetLastError());
    if (Memory) VirtualFree(Memory, 0, MEM_RELEASE);

    pSetThreadInformation = (PFN_SET_THREAD_INFORMATION)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                       "SetThreadInformation");
    if (!pSetThreadInformation)
    {
        skip("SetThreadInformation unavailable\n");
        return;
    }
    ok(pSetThreadInformation(GetCurrentThread(), ThreadDynamicCodePolicy, &Allow, sizeof(Allow)),
       "thread opt-out (THREAD_DYNAMIC_CODE_ALLOW) failed %lu\n", GetLastError());
    Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    ok(Memory != NULL, "executable memory refused after the thread opted out: %lu\n", GetLastError());
    if (Memory) VirtualFree(Memory, 0, MEM_RELEASE);
}

static void
TestStrictHandleCheck(void)
{
    NTSTATUS Code = STATUS_SUCCESS;
    BOOL Result = TRUE;

    _SEH2_TRY
    {
        Result = SetEvent((HANDLE)(ULONG_PTR)0x7FF4);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Code = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    ok(Code == STATUS_INVALID_HANDLE, "invalid handle use raised 0x%lx, result %d\n", Code, Result);
}

static void
TestChildProcess(void)
{
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH + 32];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;

    GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application));
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" ProcessMitigationPolicy none", Application);
    SetLastError(0xdeadbeef);
    if (CreateProcessW(Application, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
    {
        ok(0, "child process created under NoChildProcessCreation\n");
        WaitForSingleObject(Process.hProcess, 5000);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
        return;
    }
    ok(GetLastError() == ERROR_CHILD_PROCESS_BLOCKED, "error %lu\n", GetLastError());
}

static void
TestPolicyChild(ULONG Index)
{
    PROCESS_MITIGATION_POLICY Policy = Policies[Index].Policy;
    DWORD Flags = Policies[Index].Flags;

    ok(GetPolicy(Policy) == 0, "%s policy set in a new process: 0x%lx\n", Policies[Index].Name, GetPolicy(Policy));

    SetLastError(0xdeadbeef);
    ok(!SetPolicy(Policy, 1UL << Policies[Index].FlagCount), "%s policy took an undefined flag\n", Policies[Index].Name);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "%s undefined flag error %lu\n", Policies[Index].Name, GetLastError());
    ok(GetPolicy(Policy) == 0, "%s policy after an undefined flag: 0x%lx\n", Policies[Index].Name, GetPolicy(Policy));

    ok(SetPolicy(Policy, Flags), "%s policy 0x%lx failed %lu\n", Policies[Index].Name, Flags, GetLastError());
    ok(GetPolicy(Policy) == Flags, "%s policy reads 0x%lx, set 0x%lx\n", Policies[Index].Name, GetPolicy(Policy), Flags);

    ok(SetPolicy(Policy, Flags), "%s policy set again failed %lu\n", Policies[Index].Name, GetLastError());

    SetLastError(0xdeadbeef);
    ok(!SetPolicy(Policy, 0), "%s policy cleared\n", Policies[Index].Name);
    ok(GetLastError() == ERROR_ACCESS_DENIED, "%s clear error %lu\n", Policies[Index].Name, GetLastError());
    ok(GetPolicy(Policy) == Flags, "%s policy after a clear: 0x%lx\n", Policies[Index].Name, GetPolicy(Policy));

    if (Policy == ProcessDynamicCodePolicy) TestDynamicCode();
    else if (Policy == ProcessStrictHandleCheckPolicy) TestStrictHandleCheck();
    else if (Policy == ProcessChildProcessPolicy) TestChildProcess();
}

START_TEST(ProcessMitigationPolicy)
{
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH + 48];
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process;
    DWORD Wait, Code;
    char **argv;
    int argc = winetest_get_mainargs(&argv);
    ULONG Index;

    if (argc >= 3)
    {
        if (strcmp(argv[2], "none") && (Index = strtoul(argv[2], NULL, 10)) < ARRAYSIZE(Policies))
            TestPolicyChild(Index);
        return;
    }

    /* The policies cannot be lifted again, so each is set in a process of its own */
    GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application));
    for (Index = 0; Index < ARRAYSIZE(Policies); Index++)
    {
        StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" ProcessMitigationPolicy %lu", Application, Index);
        if (!CreateProcessW(Application, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &Startup, &Process))
        {
            ok(0, "CreateProcess for the %s policy failed %lu\n", Policies[Index].Name, GetLastError());
            continue;
        }
        Wait = WaitForSingleObject(Process.hProcess, 10000);
        ok(Wait == WAIT_OBJECT_0, "%s child wait %lu\n", Policies[Index].Name, Wait);
        if (Wait != WAIT_OBJECT_0) TerminateProcess(Process.hProcess, 1);
        Code = 1;
        GetExitCodeProcess(Process.hProcess, &Code);
        ok(Code == 0, "%s child had %lu failures\n", Policies[Index].Name, Code);
        CloseHandle(Process.hThread);
        CloseHandle(Process.hProcess);
    }
}
