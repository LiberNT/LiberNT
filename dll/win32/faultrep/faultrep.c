/* Fault report handling
 *
 * Copyright 2007 Peter Dons Tychsen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "wine/debug.h"

#include "errorrep.h"
#ifdef __REACTOS__
#include "winver.h"
#include "rpc.h"
#include <stdio.h>
#endif

WINE_DEFAULT_DEBUG_CHANNEL(faultrep);

/*************************************************************************
 * AddERExcludedApplicationW  [FAULTREP.@]
 *
 * Adds an application to a list of applications for which fault reports
 * shouldn't be generated
 *
 * PARAMS
 * lpAppFileName  [I] The filename of the application executable
 *
 * RETURNS
 * TRUE on success, FALSE of failure
 *
 * NOTES
 * Wine doesn't use this data but stores it in the registry (in the same place
 * as Windows would) in case it will be useful in a future version
 *
 */
BOOL WINAPI AddERExcludedApplicationW(LPCWSTR lpAppFileName)
{
    WCHAR *bslash;
    DWORD value = 1;
    HKEY hkey;
    LONG res;

    TRACE("(%s)\n", wine_dbgstr_w(lpAppFileName));
    bslash = wcsrchr(lpAppFileName, '\\');
    if (bslash != NULL)
        lpAppFileName = bslash + 1;
    if (*lpAppFileName == '\0')
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    res = RegCreateKeyW(HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\PCHealth\\ErrorReporting\\ExclusionList", &hkey);
    if (!res)
    {
        RegSetValueExW(hkey, lpAppFileName, 0, REG_DWORD, (LPBYTE)&value, sizeof(value));
        RegCloseKey(hkey);
    }

    return !res;
}

/*************************************************************************
 * AddERExcludedApplicationA  [FAULTREP.@]
 *
 * See AddERExcludedApplicationW
 */
BOOL WINAPI AddERExcludedApplicationA(LPCSTR lpAppFileName)
{
    int len = MultiByteToWideChar(CP_ACP, 0, lpAppFileName, -1, NULL, 0);
    WCHAR *wstr;
    BOOL ret;

    TRACE("(%s)\n", wine_dbgstr_a(lpAppFileName));
    if (len == 0)
        return FALSE;
    wstr = HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR)*len);
    MultiByteToWideChar(CP_ACP, 0, lpAppFileName, -1, wstr, len);
    ret = AddERExcludedApplicationW(wstr);
    HeapFree(GetProcessHeap(), 0, wstr);
    return ret;
}

/*************************************************************************
 * ReportFault  [FAULTREP.@]
 */
#ifdef __REACTOS__
static void report_fault_version(const WCHAR *path, WCHAR *text, DWORD size)
{
    VS_FIXEDFILEINFO *info;
    UINT info_size;
    DWORD handle, length;
    void *data;

    lstrcpynW(text, L"0.0.0.0", size);
    if (!path[0] || !(length = GetFileVersionInfoSizeW(path, &handle)))
        return;
    if (!(data = HeapAlloc(GetProcessHeap(), 0, length)))
        return;
    if (GetFileVersionInfoW(path, 0, length, data) && VerQueryValueW(data, L"\\", (void **)&info, &info_size) && info_size >= sizeof(*info))
    {
        _snwprintf(text, size, L"%u.%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                   HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
        text[size - 1] = 0;
    }
    HeapFree(GetProcessHeap(), 0, data);
}

static DWORD report_fault_timestamp(HMODULE module)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)module;
    const IMAGE_NT_HEADERS *nt;

    if (!module || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    nt = (const IMAGE_NT_HEADERS *)((const BYTE *)module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;
    return nt->FileHeader.TimeDateStamp;
}

static const WCHAR *report_fault_name(const WCHAR *path)
{
    const WCHAR *name = wcsrchr(path, '\\');

    return name ? name + 1 : path;
}

static PSID report_fault_user(BYTE *buffer, DWORD size)
{
    HANDLE token;
    DWORD needed;
    BOOL ok;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return NULL;
    ok = GetTokenInformation(token, TokenUser, buffer, size, &needed);
    CloseHandle(token);
    return ok ? ((TOKEN_USER *)buffer)->User.Sid : NULL;
}

static BOOL report_fault_launch_host(const WCHAR *directory, const WCHAR *arguments, BOOL native, PROCESS_INFORMATION *info)
{
    WCHAR path[MAX_PATH], cmd[MAX_PATH + 96];
    STARTUPINFOW startup;
    PVOID redirection = NULL;
    BOOL ok;

    if (_snwprintf(path, ARRAYSIZE(path), L"%s\\werfault.exe", directory) < 0 ||
        _snwprintf(cmd, ARRAYSIZE(cmd), L"\"%s\" %s", path, arguments) < 0)
        return FALSE;
    path[ARRAYSIZE(path) - 1] = 0;
    cmd[ARRAYSIZE(cmd) - 1] = 0;

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    if (native && !Wow64DisableWow64FsRedirection(&redirection))
        return FALSE;
    ok = CreateProcessW(path, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &startup, info);
    if (native)
        Wow64RevertWow64FsRedirection(redirection);
    return ok;
}

static EFaultRepRetVal report_fault_host(LPEXCEPTION_POINTERS pep)
{
    WCHAR self[MAX_PATH], directory[MAX_PATH], arguments[96];
    PROCESS_INFORMATION info;
    DWORD code;
    UINT len;

    if (GetModuleFileNameW(NULL, self, ARRAYSIZE(self)) && !lstrcmpiW(report_fault_name(self), L"werfault.exe"))
        return frrvErrNoDW;

    len = GetSystemDirectoryW(directory, ARRAYSIZE(directory));
    if (!len || len >= ARRAYSIZE(directory))
        return frrvErrNoDW;

    _snwprintf(arguments, ARRAYSIZE(arguments), L"-u -p %lu -t %lu -x 0x%Ix",
               GetCurrentProcessId(), GetCurrentThreadId(), (ULONG_PTR)pep);
    arguments[ARRAYSIZE(arguments) - 1] = 0;
    if (!report_fault_launch_host(directory, arguments, FALSE, &info))
        return frrvErrNoDW;

    CloseHandle(info.hThread);
    WaitForSingleObject(info.hProcess, INFINITE);
    if (!GetExitCodeProcess(info.hProcess, &code) || code > frrvErrDoubleFault)
        code = frrvErr;
    CloseHandle(info.hProcess);
    return (EFaultRepRetVal)code;
}

EFaultRepRetVal WINAPI ReportFault(LPEXCEPTION_POINTERS pep, DWORD dwOpt)
{
    WCHAR app_path[MAX_PATH], module_path[MAX_PATH], app_version[32], module_version[32];
    WCHAR app_stamp[16], module_stamp[16], code[16], offset[32], pid[16], start[32], report_id[40];
    BYTE user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    const WCHAR *strings[15];
    FILETIME creation, exit_time, kernel_time, user_time;
    ULARGE_INTEGER created;
    HMODULE module = NULL;
    ULONG_PTR address;
    RPC_WSTR uuid_text;
    HANDLE source;
    UUID uuid;
    BOOL ok;

    TRACE("%p 0x%lx\n", pep, dwOpt);
    if (!pep || !pep->ExceptionRecord)
        return frrvErrNoDW;

    address = (ULONG_PTR)pep->ExceptionRecord->ExceptionAddress;
    if (!GetModuleFileNameW(NULL, app_path, ARRAYSIZE(app_path)))
        app_path[0] = 0;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)address, &module) ||
        !GetModuleFileNameW(module, module_path, ARRAYSIZE(module_path)))
    {
        module = NULL;
        lstrcpyW(module_path, L"unknown");
    }

    report_fault_version(app_path, app_version, ARRAYSIZE(app_version));
    if (module)
        report_fault_version(module_path, module_version, ARRAYSIZE(module_version));
    else
        lstrcpyW(module_version, L"0.0.0.0");
    _snwprintf(app_stamp, ARRAYSIZE(app_stamp), L"%08lx", report_fault_timestamp(GetModuleHandleW(NULL)));
    _snwprintf(module_stamp, ARRAYSIZE(module_stamp), L"%08lx", report_fault_timestamp(module));
    _snwprintf(code, ARRAYSIZE(code), L"%08lx", pep->ExceptionRecord->ExceptionCode);
    _snwprintf(offset, ARRAYSIZE(offset), L"%0*Ix", (int)(sizeof(ULONG_PTR) * 2), module ? address - (ULONG_PTR)module : address);
    _snwprintf(pid, ARRAYSIZE(pid), L"0x%lx", GetCurrentProcessId());
    created.QuadPart = 0;
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel_time, &user_time))
    {
        created.LowPart = creation.dwLowDateTime;
        created.HighPart = creation.dwHighDateTime;
    }
    _snwprintf(start, ARRAYSIZE(start), L"0x%I64x", created.QuadPart);
    report_id[0] = 0;
    if (UuidCreate(&uuid) == RPC_S_OK && UuidToStringW(&uuid, &uuid_text) == RPC_S_OK)
    {
        lstrcpynW(report_id, (WCHAR *)uuid_text, ARRAYSIZE(report_id));
        RpcStringFreeW(&uuid_text);
    }

    strings[0] = report_fault_name(app_path);
    strings[1] = app_version;
    strings[2] = app_stamp;
    strings[3] = report_fault_name(module_path);
    strings[4] = module_version;
    strings[5] = module_stamp;
    strings[6] = code;
    strings[7] = offset;
    strings[8] = pid;
    strings[9] = start;
    strings[10] = app_path;
    strings[11] = module_path;
    strings[12] = report_id;
    strings[13] = L"";
    strings[14] = L"";

    if ((source = RegisterEventSourceW(NULL, L"Application Error")))
    {
        ok = ReportEventW(source, EVENTLOG_ERROR_TYPE, 100, 1000, report_fault_user(user, sizeof(user)), ARRAYSIZE(strings), 0, strings, NULL);
        DeregisterEventSource(source);
        if (!ok)
            WARN("Application Error event not recorded\n");
    }
    return report_fault_host(pep);
}

/*************************************************************************
 * WerReportHang  [FAULTREP.@]
 */
HRESULT WINAPI WerReportHang(HWND hwndHungApp, PCWSTR pwzHungApplicationName)
{
    DWORD (WINAPI *get_window_thread_process_id)(HWND, DWORD *);
    BOOL (WINAPI *is_window)(HWND);
    WCHAR directory[MAX_PATH], arguments[64];
    BOOL self_wow64 = FALSE, target_wow64 = FALSE, native = FALSE;
    PROCESS_INFORMATION info;
    DWORD pid = 0, tid = 0;
    HMODULE user32;
    HANDLE process;
    UINT len;

    TRACE("%p %s\n", hwndHungApp, debugstr_w(pwzHungApplicationName));
    if (pwzHungApplicationName && lstrlenW(pwzHungApplicationName) >= 128)
        return E_INVALIDARG;

    if (!(user32 = LoadLibraryW(L"user32.dll")))
        return HRESULT_FROM_WIN32(GetLastError());
    is_window = (void *)GetProcAddress(user32, "IsWindow");
    get_window_thread_process_id = (void *)GetProcAddress(user32, "GetWindowThreadProcessId");
    if (is_window && get_window_thread_process_id && hwndHungApp && is_window(hwndHungApp))
        tid = get_window_thread_process_id(hwndHungApp, &pid);
    FreeLibrary(user32);
    if (!tid || !pid)
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);

    /* FIXME: the application name is not shown: the host has no hang reporting dialog */
    if ((process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)))
    {
        IsWow64Process(process, &target_wow64);
        CloseHandle(process);
    }
    IsWow64Process(GetCurrentProcess(), &self_wow64);

    if (!self_wow64 && target_wow64)
        len = GetSystemWow64DirectoryW(directory, ARRAYSIZE(directory));
    else
        len = GetSystemDirectoryW(directory, ARRAYSIZE(directory));
    native = self_wow64 && !target_wow64;
    if (!len || len >= ARRAYSIZE(directory))
        return HRESULT_FROM_WIN32(GetLastError());

    _snwprintf(arguments, ARRAYSIZE(arguments), L"-h -p %lu -t %lu", pid, tid);
    arguments[ARRAYSIZE(arguments) - 1] = 0;
    if (!report_fault_launch_host(directory, arguments, native, &info))
        return HRESULT_FROM_WIN32(GetLastError());

    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return S_OK;
}
#else
EFaultRepRetVal WINAPI ReportFault(LPEXCEPTION_POINTERS pep, DWORD dwOpt)
{
    FIXME("%p 0x%lx stub\n", pep, dwOpt);
    return frrvOk;
}
#endif
