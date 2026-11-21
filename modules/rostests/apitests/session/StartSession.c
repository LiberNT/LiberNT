/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Start an extra session through the session manager and check it stays off the console
 * COPYRIGHT:   Copyright 2026 mikuta0407 <mikuta0407@mikuta0407.net>
 */

#include <apitest.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <wingdi.h>
#include <winuser.h>
#include <cfgmgr32.h>
#include <swdevice.h>
#include <stdio.h>
#include <ndk/ntndk.h>
#include <sm/smmsg.h>

#define CHILD_REPORT L"\\Temp\\session_child.txt"

static HANDLE AdapterReady;

static ULONG VideoDeviceCount(void)
{
    DWORD max = 0, size = sizeof(max);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\VIDEO", L"MaxObjectNumber", RRF_RT_REG_DWORD,
                     NULL, &max, &size) != ERROR_SUCCESS)
        return 0;
    return max + 1;
}

static VOID WINAPI AdapterCreated(HSWDEVICE device, HRESULT result, PVOID context, PCWSTR instance)
{
    trace("adapter %ls: 0x%08lx\n", instance ? instance : L"", result);
    if (SUCCEEDED(result)) SetEvent(AdapterReady);
}

static BOOL SessionSeesRemoteAdapter(void)
{
    DISPLAY_DEVICEW device = { sizeof(device) };
    DWORD index;
    for (index = 0; EnumDisplayDevicesW(NULL, index, &device, 0); index++)
    {
        if (wcsstr(device.DeviceString, L"Remote Display")) return TRUE;
        device.cb = sizeof(device);
    }
    return FALSE;
}

static BOOL ReadChildReport(WCHAR *text, DWORD count)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD read = 0;
    GetWindowsDirectoryW(path, MAX_PATH);
    lstrcatW(path, CHILD_REPORT);
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ReadFile(file, text, (count - 1) * sizeof(WCHAR), &read, NULL);
    CloseHandle(file);
    text[read / sizeof(WCHAR)] = 0;
    return read != 0;
}

static DWORD SessionProcessId(ULONG sessionId, PCWSTR image)
{
    PSYSTEM_PROCESS_INFORMATION processes, process;
    ULONG size = 0x100000;
    DWORD pid = 0;
    NTSTATUS status;
    for (;;)
    {
        processes = HeapAlloc(GetProcessHeap(), 0, size);
        if (!processes) return 0;
        status = NtQuerySystemInformation(SystemProcessInformation, processes, size, NULL);
        if (status != STATUS_INFO_LENGTH_MISMATCH) break;
        HeapFree(GetProcessHeap(), 0, processes);
        size *= 2;
    }
    for (process = processes; NT_SUCCESS(status); process = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)process + process->NextEntryOffset))
    {
        if (process->SessionId == sessionId && process->ImageName.Buffer &&
            process->ImageName.Length == lstrlenW(image) * sizeof(WCHAR) &&
            !_wcsnicmp(process->ImageName.Buffer, image, lstrlenW(image)))
        {
            pid = HandleToUlong(process->UniqueProcessId);
            break;
        }
        if (!process->NextEntryOffset) break;
    }
    HeapFree(GetProcessHeap(), 0, processes);
    return pid;
}

static ULONG ProcessesInSession(ULONG sessionId, BOOL exited)
{
    PSYSTEM_PROCESS_INFORMATION processes, process;
    ULONG size = 0x100000, live = 0;
    NTSTATUS status;
    for (;;)
    {
        processes = HeapAlloc(GetProcessHeap(), 0, size);
        if (!processes) return MAXULONG;
        status = NtQuerySystemInformation(SystemProcessInformation, processes, size, NULL);
        if (status != STATUS_INFO_LENGTH_MISMATCH) break;
        HeapFree(GetProcessHeap(), 0, processes);
        size *= 2;
    }
    if (!NT_SUCCESS(status))
    {
        HeapFree(GetProcessHeap(), 0, processes);
        return MAXULONG;
    }
    for (process = processes; ; process = (PSYSTEM_PROCESS_INFORMATION)((PUCHAR)process + process->NextEntryOffset))
    {
        if (process->UniqueProcessId && process->SessionId == sessionId)
        {
            HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                        HandleToUlong(process->UniqueProcessId));
            DWORD code = STILL_ACTIVE;
            if (exited || !handle || (GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE))
            {
                trace("session %lu still has %.*ls (%lu)\n", sessionId, process->ImageName.Length / 2,
                      process->ImageName.Buffer, HandleToUlong(process->UniqueProcessId));
                live++;
            }
            if (handle) CloseHandle(handle);
        }
        if (!process->NextEntryOffset) break;
    }
    HeapFree(GetProcessHeap(), 0, processes);
    return live;
}

static BOOL ProcessObjectGone(DWORD pid, ULONG sessionId)
{
    PROCESS_SESSION_INFORMATION info = { MAXULONG };
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    NTSTATUS status;

    if (!handle) return GetLastError() == ERROR_INVALID_PARAMETER;
    status = NtQueryInformationProcess(handle, ProcessSessionInformation, &info, sizeof(info), NULL);
    CloseHandle(handle);
    return NT_SUCCESS(status) && info.SessionId != sessionId;
}

static const char SessionTags[][5] =
{
    "Gla1", "Gla4", "Gla5", "Gla8", "Gh05", "Gh08", "Gh09", "Gh0:", "Gh0@", "Gh?@",
    "GDev", "Gdev", "Gmdv", "Gldv", "Gsem", "Gpff", "Gfnt", "FNTE", "CDD ", "Usts",
    "Usdc", "Ussn", "Ussy", "AtoT", "USTR", "TIMB"
};

static PSYSTEM_POOLTAG_INFORMATION PoolTags(void)
{
    PSYSTEM_POOLTAG_INFORMATION info;
    ULONG length = 1 << 20;

    info = HeapAlloc(GetProcessHeap(), 0, length);
    if (info && !NT_SUCCESS(NtQuerySystemInformation(SystemPoolTagInformation, info, length, NULL)))
    {
        HeapFree(GetProcessHeap(), 0, info);
        info = NULL;
    }
    ok(info != NULL, "SystemPoolTagInformation failed\n");
    return info;
}

static LONG PoolBlocks(PSYSTEM_POOLTAG_INFORMATION info, const char *tag)
{
    ULONG i;

    if (!info) return -1;
    for (i = 0; i < info->Count; i++)
    {
        if (!memcmp(info->TagInfo[i].Tag, tag, 4))
            return (LONG)(info->TagInfo[i].PagedAllocs - info->TagInfo[i].PagedFrees +
                          info->TagInfo[i].NonPagedAllocs - info->TagInfo[i].NonPagedFrees);
    }
    return 0;
}

static void RunSession(HANDLE port, ULONG ownSession, ULONG *previous)
{
    WCHAR commandLine[MAX_PATH + 32], path[MAX_PATH], report[1024];
    UNICODE_STRING command;
    HANDLE csrss = NULL, initial = NULL;
    ULONG sessionId = MAXULONG, wait;
    DWORD csrssPid;
    NTSTATUS status;
    int width = 0, height = 0, devices = -1, remote = -1, desktop = 0;
    DWORD modeWidth = 0, modeHeight = 0;
    DWORD childSession = MAXULONG, childConsole = MAXULONG;

    GetWindowsDirectoryW(path, MAX_PATH);
    lstrcatW(path, CHILD_REPORT);
    DeleteFileW(path);

    GetModuleFileNameW(NULL, path, MAX_PATH);
    lstrcpyW(wcsrchr(path, L'\\') + 1, L"session_child.exe");
    lstrcpynW(commandLine, path, ARRAYSIZE(commandLine));
    RtlInitUnicodeString(&command, commandLine);
    status = SmStartCsr(port, &sessionId, &command, &csrss, &initial);
    ok(status == STATUS_SUCCESS, "SmStartCsr returned 0x%lx\n", status);
    if (!NT_SUCCESS(status)) return;
    ok(sessionId != ownSession && sessionId != 0 && sessionId != *previous, "the new session is %lu\n", sessionId);
    ok(csrss != NULL && initial != NULL && csrss != initial, "the session's processes are %p and %p\n", csrss, initial);
    for (wait = 0; wait < 300 && !ReadChildReport(report, ARRAYSIZE(report)); wait++) Sleep(100);
    ok(ReadChildReport(report, ARRAYSIZE(report)), "the child in the new session wrote no report\n");
    trace("child: %ls\n", report);
    swscanf(report, L"session %lu console %lu devices %d remote %d screen %dx%d mode %lux%lu desktop %d",
            &childSession, &childConsole, &devices, &remote, &width, &height, &modeWidth, &modeHeight,
            &desktop);
    ok(childSession == sessionId, "the child ran in session %lu, not %lu\n", childSession, sessionId);
    ok(childConsole == ownSession, "the new session saw console %lu\n", childConsole);
    ok(devices == 1 && remote == 1, "the new session sees %d display devices, %d remote\n", devices, remote);
    ok(desktop == 1, "the new session could not create its desktop\n");
    ok(modeWidth > 0 && width == (int)modeWidth && height == (int)modeHeight,
       "the new session's screen is %dx%d, its display mode %lux%lu\n", width, height, modeWidth, modeHeight);
    ok(ProcessesInSession(sessionId, FALSE) >= 2, "the new session has %lu processes\n",
       ProcessesInSession(sessionId, FALSE));

    csrssPid = SessionProcessId(sessionId, L"csrss.exe");
    ok(csrssPid != 0, "session %lu has no csrss\n", sessionId);
    status = SmStopCsr(port, sessionId);
    ok(status == STATUS_SUCCESS, "SmStopCsr returned 0x%lx\n", status);
    ok(ProcessesInSession(sessionId, FALSE) == 0, "%lu processes outlived session %lu\n",
       ProcessesInSession(sessionId, FALSE), sessionId);
    CloseHandle(csrss);
    CloseHandle(initial);
    for (wait = 0; wait < 100 && ProcessesInSession(sessionId, TRUE) != 0; wait++) Sleep(100);
    ok(ProcessesInSession(sessionId, TRUE) == 0, "%lu exited processes of session %lu were never deleted\n",
       ProcessesInSession(sessionId, TRUE), sessionId);
    for (wait = 0; wait < 100 && !ProcessObjectGone(csrssPid, sessionId); wait++) Sleep(100);
    ok(ProcessObjectGone(csrssPid, sessionId), "the csrss of session %lu (%lu) was never deleted\n",
       sessionId, csrssPid);
    status = SmStopCsr(port, sessionId);
    ok(status == STATUS_INVALID_PARAMETER, "stopping session %lu again returned 0x%lx\n", sessionId, status);
    *previous = sessionId;
}

START_TEST(StartSession)
{
    static const WCHAR hardwareIds[] = L"RdpVdd_RemoteDisplay\0";
    SW_DEVICE_CREATE_INFO info = { sizeof(info) };
    HSWDEVICE adapter = NULL;
    HANDLE port = NULL;
    ULONG videoBefore, wait, previous = MAXULONG, i;
    PSYSTEM_POOLTAG_INFORMATION before, between, after;
    LONG messageLists, gdiLists;
    DWORD ownSession = 0, console;
    NTSTATUS status;
    HRESULT hr;

    ProcessIdToSessionId(GetCurrentProcessId(), &ownSession);
    console = WTSGetActiveConsoleSessionId();
    ok(console == ownSession, "the test runs in session %lu, the console is %lu\n", ownSession, console);

    AdapterReady = CreateEventW(NULL, TRUE, FALSE, NULL);
    videoBefore = VideoDeviceCount();
    info.pszInstanceId = L"RdpVdd_RemoteDisplay&NewSession";
    info.pszzHardwareIds = hardwareIds;
    info.pszzCompatibleIds = hardwareIds;
    info.CapabilityFlags = SWDeviceCapabilitiesRemovable | SWDeviceCapabilitiesSilentInstall |
                           SWDeviceCapabilitiesDriverRequired;
    info.pszDeviceDescription = L"LiberNT Remote Display Adapter";
    hr = SwDeviceCreate(L"RemoteDisplayEnum", L"HTREE\\ROOT\\0", &info, 0, NULL, AdapterCreated, NULL, &adapter);
    ok(hr == S_OK, "SwDeviceCreate returned 0x%08lx\n", hr);
    if (FAILED(hr)) return;
    ok(WaitForSingleObject(AdapterReady, 15000) == WAIT_OBJECT_0, "the adapter was not created\n");
    for (wait = 0; wait < 150 && VideoDeviceCount() <= videoBefore; wait++) Sleep(100);
    ok(VideoDeviceCount() > videoBefore, "the adapter did not start (%lu video devices)\n", VideoDeviceCount());
    ok(!SessionSeesRemoteAdapter(), "the console session took the adapter meant for a new session\n");

    status = SmConnectToSm(NULL, NULL, 0, &port);
    ok(status == STATUS_SUCCESS, "SmConnectToSm returned 0x%lx\n", status);
    if (NT_SUCCESS(status))
    {
        status = SmStopCsr(port, 0);
        ok(status == STATUS_INVALID_PARAMETER, "stopping session 0 returned 0x%lx\n", status);
        status = SmStopCsr(port, ownSession);
        ok(status == STATUS_INVALID_PARAMETER, "stopping the console session returned 0x%lx\n", status);
        before = PoolTags();
        messageLists = PoolBlocks(before, "UMSG");
        gdiLists = PoolBlocks(before, "Ghtb");
        ok(messageLists > 0 && gdiLists > 0, "the console's win32k holds %ld UMSG and %ld Ghtb blocks\n",
           messageLists, gdiLists);
        RunSession(port, ownSession, &previous);
        between = PoolTags();
        RunSession(port, ownSession, &previous);
        after = PoolTags();
        ok(PoolBlocks(after, "UMSG") == messageLists, "UMSG went from %ld to %ld blocks across two sessions\n",
           messageLists, PoolBlocks(after, "UMSG"));
        ok(PoolBlocks(after, "Ghtb") == gdiLists, "Ghtb went from %ld to %ld blocks across two sessions\n",
           gdiLists, PoolBlocks(after, "Ghtb"));
        ok(PoolBlocks(between, "Gla1") > 0 && PoolBlocks(between, "Gh0@") > 0,
           "the console's win32k holds %ld Gla1 and %ld Gh0@ blocks\n",
           PoolBlocks(between, "Gla1"), PoolBlocks(between, "Gh0@"));
        for (i = 0; i < ARRAYSIZE(SessionTags); i++)
        {
            ok(PoolBlocks(after, SessionTags[i]) == PoolBlocks(between, SessionTags[i]),
               "%s went from %ld to %ld blocks across a session\n", SessionTags[i],
               PoolBlocks(between, SessionTags[i]), PoolBlocks(after, SessionTags[i]));
        }
        HeapFree(GetProcessHeap(), 0, before);
        HeapFree(GetProcessHeap(), 0, between);
        HeapFree(GetProcessHeap(), 0, after);
        NtClose(port);
    }
    ok(WTSGetActiveConsoleSessionId() == console, "the console moved to session %lu\n", WTSGetActiveConsoleSessionId());
    ok(!SessionSeesRemoteAdapter(), "the console session sees the new session's adapter\n");
    ok(ProcessesInSession(ownSession, FALSE) > 0, "the console session lost its processes\n");
    SwDeviceClose(adapter);
}
