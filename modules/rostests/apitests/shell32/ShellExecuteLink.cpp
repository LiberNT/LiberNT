/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test ShellExecuteEx on a shortcut file with and without a verb
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <strsafe.h>
#include <shellutils.h>
#include <tlhelp32.h>

struct PROCESS_STAMP
{
    DWORD Id;
    FILETIME Created;
};

static UINT GetImageProcesses(PCWSTR Image, PROCESS_STAMP *Stamps, UINT Max)
{
    PROCESSENTRY32W Entry = { sizeof(Entry) };
    HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    FILETIME Exited, Kernel, User;
    HANDLE Process;
    UINT Count = 0;

    if (Snapshot == INVALID_HANDLE_VALUE)
        return 0;
    if (Process32FirstW(Snapshot, &Entry))
    {
        do
        {
            if (Count == Max || Entry.th32ProcessID == GetCurrentProcessId() || _wcsicmp(Entry.szExeFile, Image))
                continue;
            Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Entry.th32ProcessID);
            if (!Process)
                continue;
            if (GetProcessTimes(Process, &Stamps[Count].Created, &Exited, &Kernel, &User))
                Stamps[Count++].Id = Entry.th32ProcessID;
            CloseHandle(Process);
        } while (Process32NextW(Snapshot, &Entry));
    }
    CloseHandle(Snapshot);
    return Count;
}

/* Process ids are reused, so a process counts as new unless its id and creation time were seen before */
static BOOL NewImageProcessStarted(PCWSTR Image, const PROCESS_STAMP *Before, UINT BeforeCount)
{
    PROCESS_STAMP After[32];
    UINT AfterCount = GetImageProcesses(Image, After, ARRAYSIZE(After)), i, j;

    for (i = 0; i < AfterCount; i++)
    {
        for (j = 0; j < BeforeCount; j++)
        {
            if (After[i].Id == Before[j].Id && !CompareFileTime(&After[i].Created, &Before[j].Created))
                break;
        }
        if (j == BeforeCount)
            return TRUE;
    }
    return FALSE;
}

static HRESULT CreateLink(PCWSTR Path, PCWSTR Target, PCWSTR Arguments)
{
    CComPtr<IShellLinkW> Link;
    CComPtr<IPersistFile> File;
    HRESULT hr;

    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARG(IShellLinkW, &Link));
    if (FAILED(hr))
        return hr;
    Link->SetPath(Target);
    Link->SetArguments(Arguments);
    hr = Link->QueryInterface(IID_PPV_ARG(IPersistFile, &File));
    if (FAILED(hr))
        return hr;
    return File->Save(Path, TRUE);
}

static void TestVerb(PCWSTR Folder, PCWSTR Verb, PCWSTR Name, BOOL Runs)
{
    WCHAR Module[MAX_PATH], Link[MAX_PATH], Arguments[MAX_PATH];
    SHELLEXECUTEINFOW Info = { sizeof(Info) };
    HANDLE Event;
    BOOL Success;
    HRESULT hr;
    PROCESS_STAMP Before[32];
    UINT BeforeCount = 0;

    GetModuleFileNameW(NULL, Module, ARRAYSIZE(Module));
    StringCchPrintfW(Link, ARRAYSIZE(Link), L"%sselink-%s.lnk", Folder, Name);
    StringCchPrintfW(Arguments, ARRAYSIZE(Arguments), L"ShellExecuteLink child LiberNT-selink-%lu-%s",
                     GetCurrentProcessId(), Name);
    hr = CreateLink(Link, Module, Arguments);
    ok(hr == S_OK, "%S: creating the shortcut failed with 0x%lx\n", Name, hr);
    if (FAILED(hr))
        return;
    StringCchPrintfW(Arguments, ARRAYSIZE(Arguments), L"LiberNT-selink-%lu-%s", GetCurrentProcessId(), Name);
    Event = CreateEventW(NULL, TRUE, FALSE, Arguments);

    Info.fMask = SEE_MASK_FLAG_DDEWAIT | SEE_MASK_FLAG_NO_UI;
    Info.lpVerb = Verb;
    Info.lpFile = Link;
    Info.nShow = SW_SHOWMINNOACTIVE;
    if (!Runs)
        BeforeCount = GetImageProcesses(PathFindFileNameW(Module), Before, ARRAYSIZE(Before));
    SetLastError(0xdeadbeef);
    Success = ShellExecuteExW(&Info);
    if (Runs)
    {
        ok(Success, "%S: ShellExecuteExW failed with %lu\n", Name, GetLastError());
        ok((UINT_PTR)Info.hInstApp > 32, "%S: hInstApp is %Iu\n", Name, (UINT_PTR)Info.hInstApp);
        ok_long(WaitForSingleObject(Event, 10000), WAIT_OBJECT_0);
    }
    else
    {
        ok(!Success, "%S: ShellExecuteExW succeeded\n", Name);
        ok_long(GetLastError(), ERROR_NO_ASSOCIATION);
        ok((UINT_PTR)Info.hInstApp == SE_ERR_NOASSOC, "%S: hInstApp is %Iu\n", Name, (UINT_PTR)Info.hInstApp);
        ok_long(WaitForSingleObject(Event, 0), WAIT_TIMEOUT);
        ok(!NewImageProcessStarted(PathFindFileNameW(Module), Before, BeforeCount), "%S: a child was started\n", Name);
    }
    CloseHandle(Event);
    DeleteFileW(Link);
}

START_TEST(ShellExecuteLink)
{
    WCHAR Folder[MAX_PATH];
    char **argv;
    int argc = winetest_get_mainargs(&argv);

    if (argc > 3 && !strcmp(argv[2], "child"))
    {
        HANDLE Event = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[3]);

        if (Event)
        {
            SetEvent(Event);
            CloseHandle(Event);
        }
        return;
    }

    CoInitialize(NULL);
    GetTempPathW(ARRAYSIZE(Folder), Folder);
    TestVerb(Folder, NULL, L"default", TRUE);
    TestVerb(Folder, L"open", L"open", TRUE);
    TestVerb(Folder, L"OPEN", L"upper", TRUE);
    TestVerb(Folder, L"nosuchverb", L"unknown", FALSE);
    CoUninitialize();
}
