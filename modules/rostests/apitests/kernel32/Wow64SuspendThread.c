/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests Wow64SuspendThread, Wow64GetThreadContext and Wow64SetThreadContext
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/psfuncs.h>

typedef DWORD (WINAPI *PFN_WOW64_SUSPEND_THREAD)(HANDLE);
typedef BOOL (WINAPI *PFN_WOW64_GET_THREAD_CONTEXT)(HANDLE, PWOW64_CONTEXT);
typedef BOOL (WINAPI *PFN_WOW64_SET_THREAD_CONTEXT)(HANDLE, const WOW64_CONTEXT *);

typedef struct _WOW64_THREAD_API
{
    PCSTR Module;
    PFN_WOW64_SUSPEND_THREAD Suspend;
    PFN_WOW64_GET_THREAD_CONTEXT GetContext;
    PFN_WOW64_SET_THREAD_CONTEXT SetContext;
} WOW64_THREAD_API, *PWOW64_THREAD_API;

static
BOOL
LoadApi(
    _In_ PCWSTR ModuleName,
    _In_ PCSTR Module,
    _Out_ PWOW64_THREAD_API Api)
{
    HMODULE Handle = GetModuleHandleW(ModuleName);

    ZeroMemory(Api, sizeof(*Api));
    Api->Module = Module;
    if (!Handle)
        return FALSE;

    Api->Suspend = (PFN_WOW64_SUSPEND_THREAD)GetProcAddress(Handle, "Wow64SuspendThread");
    Api->GetContext = (PFN_WOW64_GET_THREAD_CONTEXT)GetProcAddress(Handle, "Wow64GetThreadContext");
    Api->SetContext = (PFN_WOW64_SET_THREAD_CONTEXT)GetProcAddress(Handle, "Wow64SetThreadContext");
    if (!Api->Suspend)
        skip("%s!Wow64SuspendThread is missing\n", Module);
    if (!Api->GetContext)
        skip("%s!Wow64GetThreadContext is missing\n", Module);
    if (!Api->SetContext)
        skip("%s!Wow64SetThreadContext is missing\n", Module);

    return Api->Suspend && Api->GetContext && Api->SetContext;
}

static
DWORD
WINAPI
NeverRuns(
    _In_ PVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    return 0;
}

static
void
TestInvalidHandle(
    _In_ PWOW64_THREAD_API Api)
{
    DWORD Count;

    SetLastError(0xdeadbeef);
    Count = Api->Suspend(NULL);
    ok(Count == (DWORD)-1, "%s: Wow64SuspendThread(NULL) returned %lu\n", Api->Module, Count);
    ok(GetLastError() == ERROR_INVALID_HANDLE, "%s: last error %lu\n", Api->Module, GetLastError());
}

static
void
TestNative32(
    _In_ PWOW64_THREAD_API Api)
{
    WOW64_CONTEXT Context;
    HANDLE Thread;
    DWORD Count;
    BOOL Success;

    Thread = CreateThread(NULL, 0, NeverRuns, NULL, CREATE_SUSPENDED, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Thread)
        return;

    SetLastError(0xdeadbeef);
    Count = Api->Suspend(Thread);
    ok(Count == (DWORD)-1, "%s: Wow64SuspendThread returned %lu on 32-bit Windows\n", Api->Module, Count);
    ok(GetLastError() == ERROR_INVALID_FUNCTION, "%s: last error %lu\n", Api->Module, GetLastError());

    ZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = WOW64_CONTEXT_CONTROL;
    SetLastError(0xdeadbeef);
    Success = Api->GetContext(Thread, &Context);
    ok(!Success, "%s: Wow64GetThreadContext succeeded on 32-bit Windows\n", Api->Module);
    ok(GetLastError() == ERROR_INVALID_FUNCTION, "%s: last error %lu\n", Api->Module, GetLastError());

    SetLastError(0xdeadbeef);
    Success = Api->SetContext(Thread, &Context);
    ok(!Success, "%s: Wow64SetThreadContext succeeded on 32-bit Windows\n", Api->Module);
    ok(GetLastError() == ERROR_INVALID_FUNCTION, "%s: last error %lu\n", Api->Module, GetLastError());

    TerminateThread(Thread, 0);
    WaitForSingleObject(Thread, 5000);
    CloseHandle(Thread);
}

static
void
TestWow64Caller(
    _In_ PWOW64_THREAD_API Api)
{
    WOW64_CONTEXT Context;
    HANDLE Thread;
    DWORD Count;

    Thread = CreateThread(NULL, 0, NeverRuns, NULL, CREATE_SUSPENDED, NULL);
    ok(Thread != NULL, "CreateThread failed with %lu\n", GetLastError());
    if (!Thread)
        return;

    Count = Api->Suspend(Thread);
    ok(Count == 1, "%s: Wow64SuspendThread returned %lu\n", Api->Module, Count);
    Count = ResumeThread(Thread);
    ok(Count == 2, "%s: ResumeThread returned %lu\n", Api->Module, Count);

    ZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = WOW64_CONTEXT_CONTROL | WOW64_CONTEXT_INTEGER;
    ok(Api->GetContext(Thread, &Context), "%s: Wow64GetThreadContext failed with %lu\n", Api->Module, GetLastError());
    ok(Context.Eip != 0, "%s: Eip is 0\n", Api->Module);
    ok(Context.Esp != 0, "%s: Esp is 0\n", Api->Module);
    ok(Api->SetContext(Thread, &Context), "%s: Wow64SetThreadContext failed with %lu\n", Api->Module, GetLastError());

    TerminateThread(Thread, 0);
    WaitForSingleObject(Thread, 5000);
    CloseHandle(Thread);
}

static
void
TestInitialStack(
    _In_ PPROCESS_INFORMATION ProcessInfo,
    _In_ ULONG Esp)
{
    THREAD_BASIC_INFORMATION Basic;
    SIZE_T Read = 0;
    NTSTATUS Status;
    TEB32 Teb32;
    TEB Teb;

    Status = NtQueryInformationThread(ProcessInfo->hThread, ThreadBasicInformation, &Basic, sizeof(Basic), NULL);
    ok(Status == STATUS_SUCCESS, "ThreadBasicInformation returned 0x%lx\n", Status);
    if (!NT_SUCCESS(Status))
        return;

    ok(ReadProcessMemory(ProcessInfo->hProcess, Basic.TebBaseAddress, &Teb, sizeof(Teb), &Read) && Read == sizeof(Teb),
       "Reading the child TEB failed with %lu\n", GetLastError());
    if (Read != sizeof(Teb) || Teb.WowTebOffset <= 1)
    {
        skip("No 32-bit TEB offset\n");
        return;
    }

    Read = 0;
    ok(ReadProcessMemory(ProcessInfo->hProcess, (PUCHAR)Basic.TebBaseAddress + Teb.WowTebOffset, &Teb32, sizeof(Teb32), &Read) &&
       Read == sizeof(Teb32), "Reading the child 32-bit TEB failed with %lu\n", GetLastError());
    if (Read == sizeof(Teb32))
    {
        ok(((Esp + 0xfff) & ~0xfff) == Teb32.NtTib.StackBase, "Esp 0x%lx is not at the top of the 32-bit stack 0x%lx\n",
           Esp, Teb32.NtTib.StackBase);
    }
}

static
void
TestWow64Child(
    _In_ PWOW64_THREAD_API Api,
    _In_ BOOL Primary)
{
    WCHAR Path[MAX_PATH], CommandLine[MAX_PATH + 32];
    STARTUPINFOW StartupInfo = { sizeof(StartupInfo) };
    PROCESS_INFORMATION ProcessInfo;
    WOW64_CONTEXT Context, Readback;
    BOOL Wow64 = FALSE, Success;
    DWORD Count;
    UINT Length;

    Length = GetSystemWow64DirectoryW(Path, ARRAYSIZE(Path));
    if (!Length || Length >= ARRAYSIZE(Path) - 16)
    {
        skip("No SysWOW64 directory (%lu)\n", GetLastError());
        return;
    }
    StringCchCatW(Path, ARRAYSIZE(Path), L"\\cmd.exe");
    if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
    {
        skip("No 32-bit cmd.exe\n");
        return;
    }
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" /c exit 0", Path);

    if (!CreateProcessW(Path, CommandLine, NULL, NULL, FALSE, CREATE_SUSPENDED,
                        NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ok(FALSE, "CreateProcessW failed with %lu\n", GetLastError());
        return;
    }
    ok(IsWow64Process(ProcessInfo.hProcess, &Wow64) && Wow64, "The child is not a WoW64 process\n");

    Count = Api->Suspend(ProcessInfo.hThread);
    ok(Count == 1, "%s: Wow64SuspendThread on a suspended WoW64 thread returned %lu\n", Api->Module, Count);
    Count = Api->Suspend(ProcessInfo.hThread);
    ok(Count == 2, "%s: second Wow64SuspendThread returned %lu\n", Api->Module, Count);
    Count = ResumeThread(ProcessInfo.hThread);
    ok(Count == 3, "%s: ResumeThread returned %lu\n", Api->Module, Count);
    Count = ResumeThread(ProcessInfo.hThread);
    ok(Count == 2, "%s: ResumeThread returned %lu\n", Api->Module, Count);

    memset(&Context, 0x55, sizeof(Context));
    Context.ContextFlags = WOW64_CONTEXT_ALL;
    Success = Api->GetContext(ProcessInfo.hThread, &Context);
    ok(Success, "%s: Wow64GetThreadContext failed with %lu\n", Api->Module, GetLastError());
    if (Success && Primary)
    {
        ok(Context.ContextFlags == WOW64_CONTEXT_ALL, "ContextFlags 0x%lx\n", Context.ContextFlags);
        trace("Initial Eip 0x%lx\n", Context.Eip);
        TestInitialStack(&ProcessInfo, Context.Esp);
        ok(Context.Ebp == 0, "Ebp 0x%lx\n", Context.Ebp);
        ok(Context.Ecx == 0, "Ecx 0x%lx\n", Context.Ecx);
        ok(Context.Edx == 0, "Edx 0x%lx\n", Context.Edx);
        ok(Context.Esi == 0, "Esi 0x%lx\n", Context.Esi);
        ok(Context.Edi == 0, "Edi 0x%lx\n", Context.Edi);
        ok((Context.EFlags & ~2) == 0x200, "EFlags 0x%lx\n", Context.EFlags);
        ok((WORD)Context.FloatSave.ControlWord == 0x27f, "FPU control word 0x%lx\n", Context.FloatSave.ControlWord);
        ok(*(WORD *)Context.ExtendedRegisters == 0x27f, "SSE area control word 0x%x\n", *(WORD *)Context.ExtendedRegisters);
    }

    if (Success)
    {
        Context.ContextFlags = WOW64_CONTEXT_INTEGER;
        Context.Edi = 0x13572468;
        ok(Api->SetContext(ProcessInfo.hThread, &Context),
           "%s: Wow64SetThreadContext failed with %lu\n", Api->Module, GetLastError());

        ZeroMemory(&Readback, sizeof(Readback));
        Readback.ContextFlags = WOW64_CONTEXT_INTEGER;
        ok(Api->GetContext(ProcessInfo.hThread, &Readback),
           "%s: Wow64GetThreadContext failed with %lu\n", Api->Module, GetLastError());
        ok(Readback.Edi == 0x13572468, "%s: Edi read back as 0x%lx\n", Api->Module, Readback.Edi);
    }

    TerminateProcess(ProcessInfo.hProcess, 0);
    WaitForSingleObject(ProcessInfo.hProcess, 5000);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
}

static
void
TestNativeThreadContext(
    _In_ PWOW64_THREAD_API Api)
{
    WOW64_CONTEXT Context;
    BOOL Success;

    memset(&Context, 0x55, sizeof(Context));
    Context.ContextFlags = WOW64_CONTEXT_ALL;
    SetLastError(0xdeadbeef);
    Success = Api->GetContext(GetCurrentThread(), &Context);
    ok(!Success, "%s: Wow64GetThreadContext on a 64-bit thread succeeded\n", Api->Module);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "%s: last error %lu\n", Api->Module, GetLastError());
}

START_TEST(Wow64SuspendThread)
{
    WOW64_THREAD_API Kernel32, KernelBase;
    BOOL HaveKernel32, HaveKernelBase, Wow64 = FALSE;

    HaveKernel32 = LoadApi(L"kernel32.dll", "kernel32", &Kernel32);
    HaveKernelBase = LoadApi(L"kernelbase.dll", "kernelbase", &KernelBase);

    if (sizeof(PVOID) == sizeof(ULONG) && !IsWow64Process(GetCurrentProcess(), &Wow64))
        Wow64 = FALSE;

    if (sizeof(PVOID) == sizeof(ULONG) && !Wow64)
    {
        if (HaveKernel32)
            TestNative32(&Kernel32);
        if (HaveKernelBase)
            TestNative32(&KernelBase);
        return;
    }

    if (HaveKernel32)
        TestInvalidHandle(&Kernel32);
    if (HaveKernelBase)
        TestInvalidHandle(&KernelBase);

    if (sizeof(PVOID) == sizeof(ULONG))
    {
        if (HaveKernel32)
            TestWow64Caller(&Kernel32);
        if (HaveKernelBase)
            TestWow64Caller(&KernelBase);
        return;
    }

    if (HaveKernel32)
    {
        TestNativeThreadContext(&Kernel32);
        TestWow64Child(&Kernel32, TRUE);
    }
    if (HaveKernelBase)
    {
        TestNativeThreadContext(&KernelBase);
        TestWow64Child(&KernelBase, FALSE);
    }
}
