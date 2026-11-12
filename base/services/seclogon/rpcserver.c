/*
 * PROJECT:     ReactOS Secondary Logon Service
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Secondary Logon service RPC server
 * COPYRIGHT:   Eric Kohl 2022 <eric.kohl@reactos.org>
 */

/* INCLUDES *****************************************************************/

#include "precomp.h"

#include <seclogon_s.h>

WINE_DEFAULT_DEBUG_CHANNEL(seclogon);

/* FUNCTIONS *****************************************************************/


void __RPC_FAR * __RPC_USER midl_user_allocate(SIZE_T len)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
}


void __RPC_USER midl_user_free(void __RPC_FAR * ptr)
{
    HeapFree(GetProcessHeap(), 0, ptr);
}


DWORD
StartRpcServer(VOID)
{
    NTSTATUS Status;

    Status = lpServiceGlobals->StartRpcServer(L"seclogon", ISeclogon_v1_0_s_ifspec);
    TRACE("StartRpcServer returned 0x%08lx\n", Status);

    return RtlNtStatusToDosError(Status);
}


DWORD
StopRpcServer(VOID)
{
    NTSTATUS Status;

    Status = lpServiceGlobals->StopRpcServer(ISeclogon_v1_0_s_ifspec);
    TRACE("StopRpcServer returned 0x%08lx\n", Status);

    return RtlNtStatusToDosError(Status);
}


/* A token handed over by the caller is used only for a caller that may impersonate (CreateProcessWithTokenW) */
static
BOOL
SeclTakeCallerToken(
    _In_ handle_t hBinding,
    _In_ SECL_REQUEST *pRequest,
    _Out_ PHANDLE phToken)
{
    PRIVILEGE_SET Privilege;
    HANDLE hClientToken = NULL;
    HANDLE hCallerProcess = NULL;
    HANDLE hCallerToken = NULL;
    BOOL bHeld = FALSE;
    BOOL bTaken = FALSE;
    DWORD dwError = ERROR_PRIVILEGE_NOT_HELD;
    BOOL bResult;

    *phToken = NULL;

    Privilege.PrivilegeCount = 1;
    Privilege.Control = PRIVILEGE_SET_ALL_NECESSARY;
    Privilege.Privilege[0].Attributes = 0;
    if (!LookupPrivilegeValueW(NULL, SE_IMPERSONATE_NAME, &Privilege.Privilege[0].Luid))
        return FALSE;

    if (RpcImpersonateClient(hBinding) != RPC_S_OK)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hClientToken))
    {
        if (!PrivilegeCheck(hClientToken, &Privilege, &bHeld))
            bHeld = FALSE;
        CloseHandle(hClientToken);
    }

    /* The handle is taken as the caller, so it comes only from a process the caller could take it from itself */
    if (bHeld)
    {
        hCallerProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pRequest->dwProcessId);
        bTaken = hCallerProcess != NULL &&
                 DuplicateHandle(hCallerProcess,
                                 (HANDLE)pRequest->hToken,
                                 GetCurrentProcess(),
                                 &hCallerToken,
                                 0,
                                 FALSE,
                                 DUPLICATE_SAME_ACCESS);
        dwError = GetLastError();
        if (hCallerProcess != NULL)
            CloseHandle(hCallerProcess);
    }

    RpcRevertToSelf();

    if (!bTaken)
    {
        SetLastError(dwError);
        return FALSE;
    }

    bResult = DuplicateTokenEx(hCallerToken,
                               MAXIMUM_ALLOWED,
                               NULL,
                               SecurityImpersonation,
                               TokenPrimary,
                               phToken);
    CloseHandle(hCallerToken);

    return bResult;
}


/* The caller's process is opened as the caller and its session read from its own token, never from what the request names */
static
BOOL
SeclOpenCaller(
    _In_ handle_t hBinding,
    _In_ DWORD dwProcessId,
    _Out_ PHANDLE phProcess,
    _Out_ PDWORD pdwSessionId)
{
    HANDLE hClientToken = NULL;
    DWORD dwLength;
    DWORD dwError;

    *phProcess = NULL;
    *pdwSessionId = 0;

    if (RpcImpersonateClient(hBinding) != RPC_S_OK)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }

    if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hClientToken) &&
        GetTokenInformation(hClientToken, TokenSessionId, pdwSessionId, sizeof(*pdwSessionId), &dwLength))
    {
        *phProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, dwProcessId);
    }
    dwError = GetLastError();

    if (hClientToken != NULL)
        CloseHandle(hClientToken);

    RpcRevertToSelf();

    SetLastError(dwError);
    return *phProcess != NULL;
}


VOID
__stdcall
SeclCreateProcessWithLogonW(
    _In_ handle_t hBinding,
    _In_ SECL_REQUEST *pRequest,
    _Out_ SECL_RESPONSE *pResponse)
{
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo = {0};

    PROFILEINFOW ProfileInfo;
    PVOID pEnvironment = NULL;
    DWORD dwCreationFlags;
    HANDLE hToken = NULL;
    HANDLE hTargetProcessHandle = NULL;
    DWORD SessionId;

    ULONG dwError = ERROR_SUCCESS;
    BOOL rc;

    TRACE("SeclCreateProcessWithLogonW(%p %p %p)\n", hBinding, pRequest, pResponse);

    if (pRequest != NULL)
    {
        TRACE("Username: '%S'\n", pRequest->Username);
        TRACE("Domain: '%S'\n", pRequest->Domain);
        TRACE("Password: '%S'\n", pRequest->Password);
        TRACE("ApplicationName: '%S'\n", pRequest->ApplicationName);
        TRACE("CommandLine: '%S'\n", pRequest->CommandLine);
        TRACE("CurrentDirectory: '%S'\n", pRequest->CurrentDirectory);
        TRACE("LogonFlags: 0x%lx\n", pRequest->dwLogonFlags);
        TRACE("CreationFlags: 0x%lx\n", pRequest->dwCreationFlags);
        TRACE("ProcessId: %lu\n", pRequest->dwProcessId);
    }

    if (!SeclOpenCaller(hBinding, pRequest->dwProcessId, &hTargetProcessHandle, &SessionId))
    {
        dwError = GetLastError();
        WARN("Opening the caller failed with Error %lu\n", dwError);
        goto done;
    }

    ZeroMemory(&ProfileInfo, sizeof(ProfileInfo));

    /* Logon */
    if (pRequest->hToken != 0)
    {
        rc = SeclTakeCallerToken(hBinding,
                                 pRequest,
                                 &hToken);
    }
    else
    {
        rc = LogonUser(pRequest->Username,
                       pRequest->Domain,
                       pRequest->Password,
                       LOGON32_LOGON_INTERACTIVE,
                       LOGON32_PROVIDER_DEFAULT,
                       &hToken);
    }
    if (rc == FALSE)
    {
        dwError = GetLastError();
        WARN("LogonUser() failed with Error %lu\n", dwError);
        goto done;
    }

    if (!SetTokenInformation(hToken, TokenSessionId, &SessionId, sizeof(SessionId)))
    {
        dwError = GetLastError();
        WARN("Setting the token session failed with Error %lu\n", dwError);
        goto done;
    }

    /* Load the user profile */
    if (pRequest->dwLogonFlags & LOGON_WITH_PROFILE)
    {
        ProfileInfo.dwSize = sizeof(ProfileInfo);
        ProfileInfo.lpUserName = pRequest->Username;

        rc = LoadUserProfileW(hToken,
                              &ProfileInfo);
        if (rc == FALSE)
        {
            dwError = GetLastError();
            WARN("LoadUserProfile() failed with Error %lu\n", dwError);
            goto done;
        }
    }

    /* Initialize the startup information */
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.lpDesktop = pRequest->Desktop;

    /* FIXME: Get the rest of the startup info from the caller */
    /* TODO: give the user access to the window station and desktop the process is started on */

    /* Without an environment of its own, the process gets the one made from the user's profile */
    dwCreationFlags = pRequest->dwCreationFlags;
    rc = TRUE;
    if (pRequest->Environment == NULL)
    {
        rc = CreateEnvironmentBlock(&pEnvironment, hToken, FALSE);
        dwCreationFlags |= CREATE_UNICODE_ENVIRONMENT;
    }

    /* Create Process */
    if (rc)
    {
        rc = CreateProcessAsUserW(hToken,
                                  pRequest->ApplicationName,
                                  pRequest->CommandLine,
                                  NULL,  // lpProcessAttributes,
                                  NULL,  // lpThreadAttributes,
                                  FALSE, // bInheritHandles,
                                  dwCreationFlags,
                                  pEnvironment ? pEnvironment : pRequest->Environment,
                                  pRequest->CurrentDirectory,
                                  &StartupInfo,
                                  &ProcessInfo);
    }
    if (rc == FALSE)
    {
        dwError = GetLastError();
        WARN("CreateProcessAsUser() failed with Error %lu\n", dwError);
        goto done;
    }

    /* Return process info to the caller */
    if (pResponse != NULL)
    {
        DuplicateHandle(GetCurrentProcess(),
                        ProcessInfo.hProcess,
                        hTargetProcessHandle,
                        (PHANDLE)&pResponse->hProcess,
                        0,
                        FALSE,
                        DUPLICATE_SAME_ACCESS);

        DuplicateHandle(GetCurrentProcess(),
                        ProcessInfo.hThread,
                        hTargetProcessHandle,
                        (PHANDLE)&pResponse->hThread,
                        0,
                        FALSE,
                        DUPLICATE_SAME_ACCESS);

        pResponse->dwProcessId = ProcessInfo.dwProcessId;
        pResponse->dwThreadId = ProcessInfo.dwThreadId;
    }

done:
    if (pEnvironment != NULL)
        DestroyEnvironmentBlock(pEnvironment);

    if (hTargetProcessHandle)
        CloseHandle(hTargetProcessHandle);

    if (ProcessInfo.hThread)
        CloseHandle(ProcessInfo.hThread);

    if (ProcessInfo.hProcess)
        CloseHandle(ProcessInfo.hProcess);

    if (ProfileInfo.hProfile != NULL)
        UnloadUserProfile(hToken, ProfileInfo.hProfile);

    if (hToken != NULL)
        CloseHandle(hToken);

    if (pResponse != NULL)
        pResponse->dwError = dwError;
}
