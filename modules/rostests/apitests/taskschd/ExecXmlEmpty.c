/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that task XML accepts empty string elements, as the Task Scheduler schema allows
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <apitest.h>
#include <windows.h>
#include <initguid.h>
#include <taskschd.h>

static const WCHAR TaskXmlHead[] =
    L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
    L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
    L"  <RegistrationInfo>\n"
    L"    <Author>LiberNT</Author>\n";

static const WCHAR TaskXmlTail[] =
    L"  </RegistrationInfo>\n"
    L"  <Settings>\n"
    L"    <Enabled>true</Enabled>\n"
    L"  </Settings>\n"
    L"  <Actions Context=\"Author\">\n"
    L"    <Exec>\n"
    L"      <Command>\"C:\\Windows\\System32\\cmd.exe\"</Command>\n"
    L"      %ls\n"
    L"\t  <WorkingDirectory>\"C:\\Windows\"</WorkingDirectory>\n"
    L"    </Exec>\n"
    L"  </Actions>\n"
    L"</Task>\n";

static void CheckXml(ITaskService *Service, PCWSTR Description, PCWSTR Arguments, PCWSTR ExpectedArguments,
                     PCWSTR ExpectedDescription)
{
    WCHAR Xml[2048], Tail[1024];
    ITaskDefinition *Definition = NULL;
    IRegistrationInfo *Info = NULL;
    IActionCollection *Actions = NULL;
    IAction *Action = NULL;
    IExecAction *Exec = NULL;
    BSTR Text, Value = NULL;
    LONG Count = 0;
    HRESULT hr;

    swprintf(Tail, ARRAYSIZE(Tail), TaskXmlTail, Arguments);
    swprintf(Xml, ARRAYSIZE(Xml), L"%ls%ls%ls", TaskXmlHead, Description, Tail);

    hr = ITaskService_NewTask(Service, 0, &Definition);
    ok(hr == S_OK, "NewTask returned 0x%08lx\n", hr);
    if (FAILED(hr))
        return;

    Text = SysAllocString(Xml);
    hr = ITaskDefinition_put_XmlText(Definition, Text);
    SysFreeString(Text);
    ok(hr == S_OK, "put_XmlText with %s and %s returned 0x%08lx\n", wine_dbgstr_w(Description), wine_dbgstr_w(Arguments), hr);
    if (FAILED(hr))
    {
        ITaskDefinition_Release(Definition);
        return;
    }

    hr = ITaskDefinition_get_RegistrationInfo(Definition, &Info);
    ok(hr == S_OK, "get_RegistrationInfo returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IRegistrationInfo_get_Description(Info, &Value);
        ok(hr == S_OK, "get_Description returned 0x%08lx\n", hr);
        ok((Value ? Value : L"")[0] == 0 ? !ExpectedDescription[0] : !wcscmp(Value, ExpectedDescription),
           "Description %s, expected %s\n", wine_dbgstr_w(Value), wine_dbgstr_w(ExpectedDescription));
        SysFreeString(Value);
        Value = NULL;
        IRegistrationInfo_Release(Info);
    }

    hr = ITaskDefinition_get_Actions(Definition, &Actions);
    ok(hr == S_OK, "get_Actions returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IActionCollection_get_Count(Actions, &Count);
        ok(hr == S_OK && Count == 1, "get_Count returned 0x%08lx, count %ld\n", hr, Count);
        if (Count == 1 && SUCCEEDED(IActionCollection_get_Item(Actions, 1, &Action)))
        {
            hr = IAction_QueryInterface(Action, &IID_IExecAction, (void **)&Exec);
            ok(hr == S_OK, "QueryInterface(IExecAction) returned 0x%08lx\n", hr);
            if (SUCCEEDED(hr))
            {
                hr = IExecAction_get_Arguments(Exec, &Value);
                ok(hr == S_OK, "get_Arguments returned 0x%08lx\n", hr);
                ok((Value ? Value : L"")[0] == 0 ? !ExpectedArguments[0] : !wcscmp(Value, ExpectedArguments),
                   "Arguments %s, expected %s\n", wine_dbgstr_w(Value), wine_dbgstr_w(ExpectedArguments));
                SysFreeString(Value);
                Value = NULL;
                hr = IExecAction_get_WorkingDirectory(Exec, &Value);
                ok(hr == S_OK && Value && !wcscmp(Value, L"\"C:\\Windows\""), "get_WorkingDirectory returned 0x%08lx, %s\n",
                   hr, wine_dbgstr_w(Value));
                SysFreeString(Value);
                IExecAction_Release(Exec);
            }
            IAction_Release(Action);
        }
        IActionCollection_Release(Actions);
    }
    ITaskDefinition_Release(Definition);
}

START_TEST(ExecXmlEmpty)
{
    ITaskService *Service = NULL;
    VARIANT Empty;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(hr == S_OK, "CoInitializeEx returned 0x%08lx\n", hr);
    hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskService, (void **)&Service);
    ok(hr == S_OK, "CoCreateInstance(TaskScheduler) returned 0x%08lx\n", hr);
    if (FAILED(hr))
    {
        CoUninitialize();
        return;
    }
    V_VT(&Empty) = VT_EMPTY;
    hr = ITaskService_Connect(Service, Empty, Empty, Empty, Empty);
    ok(hr == S_OK, "Connect returned 0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        CheckXml(Service, L"", L"<Arguments>/c exit 0</Arguments>", L"/c exit 0", L"");
        CheckXml(Service, L"", L"<Arguments></Arguments>", L"", L"");
        CheckXml(Service, L"", L"<Arguments/>", L"", L"");
        CheckXml(Service, L"    <Description></Description>\n", L"<Arguments></Arguments>", L"", L"");
        CheckXml(Service, L"    <Description/>\n", L"<Arguments>x</Arguments>", L"x", L"");
        CheckXml(Service, L"    <Description>Task</Description>\n", L"<Arguments></Arguments>", L"", L"Task");
    }
    ITaskService_Release(Service);
    CoUninitialize();
}
