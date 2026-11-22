/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     IRegisteredTask::Run starts the task's Exec action with its parameters
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <windows.h>
#include <initguid.h>
#include <taskschd.h>

#define TASK_NAME L"LiberNTRunTaskTest"

static const WCHAR TaskXml[] =
    L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
    L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
    L"  <Principals><Principal id=\"Author\"><LogonType>InteractiveToken</LogonType></Principal></Principals>\n"
    L"  <Settings>\n"
    L"    <Enabled>%ls</Enabled>\n"
    L"    <AllowStartOnDemand>%ls</AllowStartOnDemand>\n"
    L"  </Settings>\n"
    L"  <Actions Context=\"Author\">\n"
    L"    <Exec>\n"
    L"      <Command>%%SystemRoot%%\\System32\\cmd.exe</Command>\n"
    L"      <Arguments>/c echo $(Arg0)&gt;\"%ls\"</Arguments>\n"
    L"    </Exec>\n"
    L"  </Actions>\n"
    L"</Task>\n";

static IRegisteredTask *Register(ITaskFolder *Root, PCWSTR Enabled, PCWSTR OnDemand, PCWSTR Output)
{
    WCHAR Xml[2048];
    IRegisteredTask *Task = NULL;
    VARIANT Empty;
    BSTR Name, Text;
    HRESULT hr;

    swprintf(Xml, ARRAYSIZE(Xml), TaskXml, Enabled, OnDemand, Output);
    VariantInit(&Empty);
    Name = SysAllocString(TASK_NAME);
    Text = SysAllocString(Xml);
    hr = ITaskFolder_RegisterTask(Root, Name, Text, TASK_CREATE_OR_UPDATE, Empty, Empty,
                                  TASK_LOGON_INTERACTIVE_TOKEN, Empty, &Task);
    SysFreeString(Text);
    SysFreeString(Name);
    ok(hr == S_OK, "RegisterTask (Enabled %ls, AllowStartOnDemand %ls) returned 0x%08lx\n", Enabled, OnDemand, hr);
    return SUCCEEDED(hr) ? Task : NULL;
}

static BOOL ReadOutput(PCWSTR Output, char *Text, DWORD Size)
{
    DWORD Read = 0, Start = GetTickCount();
    HANDLE File;

    while (GetTickCount() - Start < 3000)
    {
        File = CreateFileW(Output, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (File != INVALID_HANDLE_VALUE)
        {
            BOOL Done = ReadFile(File, Text, Size - 1, &Read, NULL) && Read != 0 && Text[Read - 1] == '\n';

            CloseHandle(File);
            if (Done)
            {
                Text[Read] = 0;
                return TRUE;
            }
        }
        Sleep(10);
    }
    return FALSE;
}

static void TestRun(ITaskFolder *Root, PCWSTR Output)
{
    IRegisteredTask *Task = Register(Root, L"true", L"true", Output);
    IRunningTask *Running = NULL;
    char Text[64];
    VARIANT Params;
    BSTR Name = NULL;
    DWORD Start;
    HRESULT hr;

    if (Task == NULL)
        return;
    V_VT(&Params) = VT_BSTR;
    V_BSTR(&Params) = SysAllocString(L"liberntran");
    Start = GetTickCount();
    hr = IRegisteredTask_Run(Task, Params, &Running);
    VariantClear(&Params);
    ok(hr == S_OK, "Run returned 0x%08lx\n", hr);
    ok(Running != NULL, "Run returned no running task\n");
    if (Running != NULL)
    {
        hr = IRunningTask_get_Name(Running, &Name);
        ok(hr == S_OK && Name != NULL && !wcscmp(Name, TASK_NAME), "running task name %s, 0x%08lx\n", wine_dbgstr_w(Name), hr);
        SysFreeString(Name);
        IRunningTask_Release(Running);
    }
    if (hr == S_OK)
    {
        ok(ReadOutput(Output, Text, sizeof(Text)), "the action wrote nothing to %s\n", wine_dbgstr_w(Output));
        ok(strstr(Text, "liberntran") != NULL, "the action did not get its parameter: %s\n", Text);
        trace("the action ran within %lu ms\n", GetTickCount() - Start);
    }
    IRegisteredTask_Release(Task);
}

static void TestRefused(ITaskFolder *Root, PCWSTR Enabled, PCWSTR OnDemand, HRESULT Expected, PCWSTR Output)
{
    IRegisteredTask *Task = Register(Root, Enabled, OnDemand, Output);
    IRunningTask *Running = NULL;
    VARIANT Params;
    HRESULT hr;

    if (Task == NULL)
        return;
    VariantInit(&Params);
    hr = IRegisteredTask_Run(Task, Params, &Running);
    ok(hr == Expected, "Run (Enabled %ls, AllowStartOnDemand %ls) returned 0x%08lx, expected 0x%08lx\n",
       Enabled, OnDemand, hr, Expected);
    if (Running != NULL)
        IRunningTask_Release(Running);
    IRegisteredTask_Release(Task);
}

START_TEST(RunTask)
{
    ITaskService *Service = NULL;
    ITaskFolder *Root = NULL;
    WCHAR Output[MAX_PATH];
    VARIANT Empty;
    BSTR Path;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskService, (void **)&Service);
    ok(hr == S_OK, "CoCreateInstance returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;
    VariantInit(&Empty);
    hr = ITaskService_Connect(Service, Empty, Empty, Empty, Empty);
    ok(hr == S_OK, "Connect returned 0x%08lx\n", hr);
    Path = SysAllocString(L"\\");
    if (SUCCEEDED(hr))
        hr = ITaskService_GetFolder(Service, Path, &Root);
    SysFreeString(Path);
    ok(hr == S_OK, "GetFolder returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        GetTempPathW(ARRAYSIZE(Output), Output);
        wcscat(Output, L"libernt_runtask.txt");
        DeleteFileW(Output);
        TestRun(Root, Output);
        TestRefused(Root, L"false", L"true", SCHED_E_TASK_DISABLED, Output);
        TestRefused(Root, L"true", L"false", SCHED_E_START_ON_DEMAND, Output);
        Path = SysAllocString(TASK_NAME);
        ITaskFolder_DeleteTask(Root, Path, 0);
        SysFreeString(Path);
        DeleteFileW(Output);
        ITaskFolder_Release(Root);
    }
    ITaskService_Release(Service);
    CoUninitialize();
}
