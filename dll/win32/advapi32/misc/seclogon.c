/*
 * PROJECT:     LiberNT system libraries
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Processes the secondary logon service starts for a caller
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <advapi32.h>

WINE_DEFAULT_DEBUG_CHANNEL(advapi);

typedef HANDLE (WINAPI *PGET_PROCESS_WINDOW_STATION)(VOID);
typedef HANDLE (WINAPI *PGET_THREAD_DESKTOP)(DWORD);
typedef BOOL (WINAPI *PGET_USER_OBJECT_INFORMATION)(HANDLE, int, PVOID, DWORD, LPDWORD);

/* UOI_NAME, winuser.h */
#define SECL_UOI_NAME 2

static
handle_t
SeclBind(VOID)
{
    RPC_WSTR StringBinding = NULL;
    handle_t hBinding = NULL;

    if (RpcStringBindingComposeW(NULL,
                                 (RPC_WSTR)L"ncacn_np",
                                 NULL,
                                 (RPC_WSTR)L"\\pipe\\seclogon",
                                 NULL,
                                 &StringBinding) != RPC_S_OK)
    {
        return NULL;
    }

    if (RpcBindingFromStringBindingW(StringBinding, &hBinding) != RPC_S_OK)
        hBinding = NULL;

    RpcStringFreeW(&StringBinding);
    return hBinding;
}

/* The window station and desktop of the caller, where a process that names none is started (CreateProcessWithTokenW) */
static
BOOL
SeclCallerDesktop(
    _Out_writes_(Length) PWSTR Desktop,
    _In_ DWORD Length)
{
    PGET_PROCESS_WINDOW_STATION pGetProcessWindowStation;
    PGET_THREAD_DESKTOP pGetThreadDesktop;
    PGET_USER_OBJECT_INFORMATION pGetUserObjectInformationW;
    WCHAR Station[MAX_PATH], Name[MAX_PATH];
    HMODULE hUser32;
    HANDLE hStation;
    HANDLE hDesktop;
    int Written;

    /* A process that never used a window station has none to hand down */
    hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32 == NULL)
        return FALSE;

    pGetProcessWindowStation = (PGET_PROCESS_WINDOW_STATION)GetProcAddress(hUser32, "GetProcessWindowStation");
    pGetThreadDesktop = (PGET_THREAD_DESKTOP)GetProcAddress(hUser32, "GetThreadDesktop");
    pGetUserObjectInformationW = (PGET_USER_OBJECT_INFORMATION)GetProcAddress(hUser32, "GetUserObjectInformationW");
    if (pGetProcessWindowStation == NULL || pGetThreadDesktop == NULL || pGetUserObjectInformationW == NULL)
        return FALSE;

    hStation = pGetProcessWindowStation();
    hDesktop = pGetThreadDesktop(GetCurrentThreadId());
    if (hStation == NULL || hDesktop == NULL ||
        !pGetUserObjectInformationW(hStation, SECL_UOI_NAME, Station, sizeof(Station), NULL) ||
        !pGetUserObjectInformationW(hDesktop, SECL_UOI_NAME, Name, sizeof(Name), NULL))
    {
        return FALSE;
    }

    Written = _snwprintf(Desktop, Length, L"%s\\%s", Station, Name);
    return Written > 0 && (DWORD)Written < Length;
}

static
DWORD
SeclEnvironmentSize(
    _In_ PVOID Environment,
    _In_ DWORD dwCreationFlags)
{
    SIZE_T Length = 0;

    if (Environment == NULL)
        return 0;

    if (dwCreationFlags & CREATE_UNICODE_ENVIRONMENT)
    {
        PCWSTR p = Environment;

        while (p[Length] != UNICODE_NULL || p[Length + 1] != UNICODE_NULL)
            Length++;

        return (DWORD)((Length + 2) * sizeof(WCHAR));
    }
    else
    {
        PCSTR p = Environment;

        while (p[Length] != ANSI_NULL || p[Length + 1] != ANSI_NULL)
            Length++;

        return (DWORD)(Length + 2);
    }
}

/* The user a token stands for, by name, which loading the profile needs */
static
BOOL
SeclTokenUserName(
    _In_ HANDLE hToken,
    _Out_writes_(UserLength) PWSTR User,
    _In_ DWORD UserLength,
    _Out_writes_(DomainLength) PWSTR Domain,
    _In_ DWORD DomainLength)
{
    union
    {
        TOKEN_USER User;
        BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    } Identity;
    SID_NAME_USE Use;
    DWORD Length;

    if (!GetTokenInformation(hToken, TokenUser, &Identity, sizeof(Identity), &Length))
        return FALSE;

    return LookupAccountSidW(NULL, Identity.User.User.Sid, User, &UserLength, Domain, &DomainLength, &Use);
}

BOOL
WINAPI
CreateProcessWithTokenW(
    _In_ HANDLE hToken,
    _In_ DWORD dwLogonFlags,
    _In_opt_ LPCWSTR lpApplicationName,
    _Inout_opt_ LPWSTR lpCommandLine,
    _In_ DWORD dwCreationFlags,
    _In_opt_ LPVOID lpEnvironment,
    _In_opt_ LPCWSTR lpCurrentDirectory,
    _In_ LPSTARTUPINFOW lpStartupInfo,
    _Out_ LPPROCESS_INFORMATION lpProcessInformation)
{
    WCHAR Desktop[MAX_PATH * 2], Directory[MAX_PATH], User[MAX_PATH], Domain[MAX_PATH];
    SECL_REQUEST Request;
    SECL_RESPONSE Response;
    handle_t hBinding;
    DWORD dwError = ERROR_SUCCESS;

    TRACE("%p 0x%lx %s %s 0x%lx %p %s %p %p\n", hToken, dwLogonFlags, debugstr_w(lpApplicationName),
          debugstr_w(lpCommandLine), dwCreationFlags, lpEnvironment, debugstr_w(lpCurrentDirectory),
          lpStartupInfo, lpProcessInformation);

    if (hToken == NULL || lpStartupInfo == NULL || lpProcessInformation == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ZeroMemory(&Request, sizeof(Request));
    ZeroMemory(&Response, sizeof(Response));

    /* A process with no desktop of its own goes where the caller is */
    if (lpStartupInfo->lpDesktop != NULL && lpStartupInfo->lpDesktop[0] != UNICODE_NULL)
        Request.Desktop = lpStartupInfo->lpDesktop;
    else if (SeclCallerDesktop(Desktop, ARRAYSIZE(Desktop)))
        Request.Desktop = Desktop;

    /* Without a directory of its own, the process starts where the caller is */
    if (lpCurrentDirectory == NULL)
    {
        DWORD Length = GetCurrentDirectoryW(ARRAYSIZE(Directory), Directory);

        if (Length != 0 && Length < ARRAYSIZE(Directory))
            lpCurrentDirectory = Directory;
    }

    if ((dwLogonFlags & LOGON_WITH_PROFILE) &&
        SeclTokenUserName(hToken, User, ARRAYSIZE(User), Domain, ARRAYSIZE(Domain)))
    {
        Request.Username = User;
        Request.Domain = Domain;
    }

    /* These three are on whatever the caller asks for (CreateProcessWithTokenW) */
    dwCreationFlags |= CREATE_DEFAULT_ERROR_MODE | CREATE_NEW_PROCESS_GROUP;
    if (!(dwCreationFlags & DETACHED_PROCESS))
        dwCreationFlags |= CREATE_NEW_CONSOLE;

    Request.ApplicationName = (LPWSTR)lpApplicationName;
    Request.CommandLine = lpCommandLine;
    Request.CurrentDirectory = (LPWSTR)lpCurrentDirectory;
    Request.Environment = lpEnvironment;
    Request.dwEnvironmentSize = SeclEnvironmentSize(lpEnvironment, dwCreationFlags);
    Request.dwLogonFlags = dwLogonFlags;
    Request.dwCreationFlags = dwCreationFlags;
    Request.dwProcessId = GetCurrentProcessId();
    Request.hToken = (DWORD_PTR)hToken;

    hBinding = SeclBind();
    if (hBinding == NULL)
    {
        SetLastError(ERROR_SERVICE_NOT_ACTIVE);
        return FALSE;
    }

    RpcTryExcept
    {
        SeclCreateProcessWithLogonW(hBinding, &Request, &Response);
        dwError = Response.dwError;
    }
    RpcExcept(EXCEPTION_EXECUTE_HANDLER)
    {
        dwError = RpcExceptionCode();
    }
    RpcEndExcept;

    RpcBindingFree(&hBinding);

    if (dwError != ERROR_SUCCESS)
    {
        SetLastError(dwError);
        return FALSE;
    }

    lpProcessInformation->hProcess = (HANDLE)Response.hProcess;
    lpProcessInformation->hThread = (HANDLE)Response.hThread;
    lpProcessInformation->dwProcessId = Response.dwProcessId;
    lpProcessInformation->dwThreadId = Response.dwThreadId;

    return TRUE;
}
