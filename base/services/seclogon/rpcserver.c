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
    RPC_STATUS RpcStatus;

    RpcStatus = RpcServerUseProtseqEpW(L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, L"seclogon", NULL);
    if (RpcStatus != RPC_S_OK && RpcStatus != RPC_S_DUPLICATE_ENDPOINT)
        WARN("RpcServerUseProtseqEpW(ncalrpc) failed with %lu\n", RpcStatus);

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


VOID
__stdcall
SeclCreateProcessWithLogonW(
    _In_ handle_t hBinding,
    _In_ SECL_REQUEST *pRequest,
    _Out_ SECL_RESPONSE *pResponse)
{
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;

    PROFILEINFOW ProfileInfo;
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

    hTargetProcessHandle = OpenProcess(PROCESS_DUP_HANDLE,
                                       FALSE,
                                       pRequest->dwProcessId);
    if (hTargetProcessHandle == NULL)
    {
        dwError = GetLastError();
        WARN("OpenProcess() failed with Error %lu\n", dwError);
        goto done;
    }

    ZeroMemory(&ProfileInfo, sizeof(ProfileInfo));

    /* Logon */
    rc = LogonUser(pRequest->Username,
                   pRequest->Domain,
                   pRequest->Password,
                   LOGON32_LOGON_INTERACTIVE,
                   LOGON32_PROVIDER_DEFAULT,
                   &hToken);
    if (rc == FALSE)
    {
        dwError = GetLastError();
        WARN("LogonUser() failed with Error %lu\n", dwError);
        goto done;
    }

    if (!ProcessIdToSessionId(pRequest->dwProcessId, &SessionId) ||
        !SetTokenInformation(hToken, TokenSessionId, &SessionId, sizeof(SessionId)))
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

    /* FIXME: Get startup info from the caller */

    /* Initialize the process information */
    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

    /* Create Process */
    rc = CreateProcessAsUserW(hToken,
                              pRequest->ApplicationName,
                              pRequest->CommandLine,
                              NULL,  // lpProcessAttributes,
                              NULL,  // lpThreadAttributes,
                              FALSE, // bInheritHandles,
                              pRequest->dwCreationFlags,
                              pRequest->Environment,  // lpEnvironment,
                              pRequest->CurrentDirectory,
                              &StartupInfo,
                              &ProcessInfo);
    if (rc == FALSE)
    {
        dwError = GetLastError();
        WARN("CreateProcessAsUser() failed with Error %lu\n", dwError);
        goto done;
    }

    /* Return process info to the caller */
    if (pResponse != NULL)
    {
        HANDLE hProcess = NULL, hThread = NULL;

        DuplicateHandle(GetCurrentProcess(),
                        ProcessInfo.hProcess,
                        hTargetProcessHandle,
                        &hProcess,
                        0,
                        FALSE,
                        DUPLICATE_SAME_ACCESS);

        DuplicateHandle(GetCurrentProcess(),
                        ProcessInfo.hThread,
                        hTargetProcessHandle,
                        &hThread,
                        0,
                        FALSE,
                        DUPLICATE_SAME_ACCESS);

        pResponse->hProcess = HandleToUlong(hProcess);
        pResponse->hThread = HandleToUlong(hThread);

        pResponse->dwProcessId = ProcessInfo.dwProcessId;
        pResponse->dwThreadId = ProcessInfo.dwThreadId;
    }

done:
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


#define SECL_TOKEN_ACCESS (TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | \
                           TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID)

typedef struct _PROFILE_RELEASE
{
    HANDLE hProcess;
    HANDLE hToken;
    HANDLE hProfile;
    HANDLE hWait;
} PROFILE_RELEASE, *PPROFILE_RELEASE;


static
VOID
CALLBACK
ReleaseProfileCallback(
    _In_ PVOID pContext,
    _In_ BOOLEAN bTimedOut)
{
    PPROFILE_RELEASE pRelease = pContext;

    UnloadUserProfile(pRelease->hToken, pRelease->hProfile);
    UnregisterWait(pRelease->hWait);
    CloseHandle(pRelease->hToken);
    CloseHandle(pRelease->hProcess);
    HeapFree(GetProcessHeap(), 0, pRelease);
}


static
BOOL
ReleaseProfileOnExit(
    _In_ HANDLE hProcess,
    _In_ HANDLE hToken,
    _In_ HANDLE hProfile)
{
    PPROFILE_RELEASE pRelease;

    pRelease = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pRelease));
    if (pRelease == NULL)
        return FALSE;

    pRelease->hProfile = hProfile;
    if (!DuplicateHandle(GetCurrentProcess(), hProcess, GetCurrentProcess(), &pRelease->hProcess,
                         SYNCHRONIZE, FALSE, 0) ||
        !DuplicateHandle(GetCurrentProcess(), hToken, GetCurrentProcess(), &pRelease->hToken,
                         0, FALSE, DUPLICATE_SAME_ACCESS) ||
        !RegisterWaitForSingleObject(&pRelease->hWait, pRelease->hProcess, ReleaseProfileCallback,
                                     pRelease, INFINITE, WT_EXECUTEONLYONCE))
    {
        if (pRelease->hToken)
            CloseHandle(pRelease->hToken);
        if (pRelease->hProcess)
            CloseHandle(pRelease->hProcess);
        HeapFree(GetProcessHeap(), 0, pRelease);
        return FALSE;
    }

    return TRUE;
}


static
BOOL
IsValidEnvironment(
    _In_ SECL_REQUEST *pRequest)
{
    const BYTE *pbEnd;

    if (pRequest->Environment == NULL)
        return TRUE;

    if (pRequest->dwCreationFlags & CREATE_UNICODE_ENVIRONMENT)
    {
        if (pRequest->dwEnvironmentSize < 2 * sizeof(WCHAR) || pRequest->dwEnvironmentSize % sizeof(WCHAR))
            return FALSE;
        pbEnd = pRequest->Environment + pRequest->dwEnvironmentSize;
        return ((const WCHAR *)pbEnd)[-1] == UNICODE_NULL && ((const WCHAR *)pbEnd)[-2] == UNICODE_NULL;
    }

    if (pRequest->dwEnvironmentSize < 2)
        return FALSE;
    pbEnd = pRequest->Environment + pRequest->dwEnvironmentSize;
    return pbEnd[-1] == 0 && pbEnd[-2] == 0;
}


static
DWORD
CheckClientPrivilege(
    _In_ handle_t hBinding,
    _In_ PCWSTR PrivilegeName)
{
    PRIVILEGE_SET PrivilegeSet;
    HANDLE hClientToken = NULL;
    BOOL bHeld = FALSE;
    DWORD dwError;

    dwError = RpcImpersonateClient(hBinding);
    if (dwError != RPC_S_OK)
        return dwError;

    PrivilegeSet.PrivilegeCount = 1;
    PrivilegeSet.Control = PRIVILEGE_SET_ALL_NECESSARY;
    PrivilegeSet.Privilege[0].Attributes = 0;
    if (!LookupPrivilegeValueW(NULL, PrivilegeName, &PrivilegeSet.Privilege[0].Luid) ||
        !OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &hClientToken) ||
        !PrivilegeCheck(hClientToken, &PrivilegeSet, &bHeld))
    {
        dwError = GetLastError();
    }
    else
    {
        dwError = bHeld ? ERROR_SUCCESS : ERROR_PRIVILEGE_NOT_HELD;
    }

    if (hClientToken)
        CloseHandle(hClientToken);
    RpcRevertToSelf();
    return dwError;
}


static
DWORD
GetTokenUserName(
    _In_ HANDLE hToken,
    _Out_writes_(cchName) PWSTR pszName,
    _In_ DWORD cchName)
{
    WCHAR szDomain[MAX_PATH];
    DWORD cchDomain = ARRAYSIZE(szDomain), cbUser = 0;
    SID_NAME_USE Use;
    PTOKEN_USER pUser;
    DWORD dwError = ERROR_SUCCESS;

    GetTokenInformation(hToken, TokenUser, NULL, 0, &cbUser);
    pUser = HeapAlloc(GetProcessHeap(), 0, cbUser);
    if (!pUser)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!GetTokenInformation(hToken, TokenUser, pUser, cbUser, &cbUser) ||
        !LookupAccountSidW(NULL, pUser->User.Sid, pszName, &cchName, szDomain, &cchDomain, &Use))
    {
        dwError = GetLastError();
    }
    HeapFree(GetProcessHeap(), 0, pUser);
    return dwError;
}


typedef struct _SECL_TOKEN_CALL
{
    ULONG ClientPid;
    HANDLE hCallerProcess;
    HANDLE hCallerToken;
    HANDLE hPrimaryToken;
    PVOID pEnvironment;
    PPROC_THREAD_ATTRIBUTE_LIST pAttributes;
    HANDLE ahInherit[3];
    ULONG cInherit;
    PROFILEINFOW ProfileInfo;
    PROCESS_INFORMATION ProcessInfo;
    WCHAR szUserName[UNLEN + 1];
} SECL_TOKEN_CALL, *PSECL_TOKEN_CALL;


static
DWORD
CheckTokenRequest(
    _In_ handle_t hBinding,
    _In_ SECL_REQUEST *pRequest,
    _Inout_ PSECL_TOKEN_CALL pCall)
{
    DWORD dwError;

    dwError = I_RpcBindingInqLocalClientPID(hBinding, &pCall->ClientPid);
    if (dwError != RPC_S_OK)
        return dwError;

    dwError = CheckClientPrivilege(hBinding, SE_IMPERSONATE_NAME);
    if (dwError != ERROR_SUCCESS)
        return dwError;

    if ((pRequest->dwCreationFlags & (DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS | DETACHED_PROCESS)) ||
        !IsValidEnvironment(pRequest))
    {
        return ERROR_INVALID_PARAMETER;
    }

    return ERROR_SUCCESS;
}


static
DWORD
GetCallerPrimaryToken(
    _In_ DWORD hToken,
    _Inout_ PSECL_TOKEN_CALL pCall)
{
    OBJECT_BASIC_INFORMATION BasicInfo;
    NTSTATUS Status;
    TOKEN_TYPE Type;
    DWORD dwSessionId;
    DWORD cbReturned;

    pCall->hCallerProcess = OpenProcess(PROCESS_DUP_HANDLE | PROCESS_CREATE_PROCESS | PROCESS_QUERY_LIMITED_INFORMATION,
                                        FALSE,
                                        pCall->ClientPid);
    if (pCall->hCallerProcess == NULL)
        return GetLastError();

    if (!DuplicateHandle(pCall->hCallerProcess,
                         LongToHandle(hToken),
                         GetCurrentProcess(),
                         &pCall->hCallerToken,
                         0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS))
    {
        return GetLastError();
    }

    Status = NtQueryObject(pCall->hCallerToken, ObjectBasicInformation, &BasicInfo, sizeof(BasicInfo), NULL);
    if (!NT_SUCCESS(Status))
        return RtlNtStatusToDosError(Status);
    if ((BasicInfo.GrantedAccess & SECL_TOKEN_ACCESS) != SECL_TOKEN_ACCESS)
        return ERROR_ACCESS_DENIED;

    if (!GetTokenInformation(pCall->hCallerToken, TokenType, &Type, sizeof(Type), &cbReturned) ||
        !ProcessIdToSessionId(pCall->ClientPid, &dwSessionId))
    {
        return GetLastError();
    }

    if (Type == TokenPrimary)
    {
        pCall->hPrimaryToken = pCall->hCallerToken;
        pCall->hCallerToken = NULL;
    }
    else if (!DuplicateTokenEx(pCall->hCallerToken,
                               TOKEN_ALL_ACCESS,
                               NULL,
                               SecurityImpersonation,
                               TokenPrimary,
                               &pCall->hPrimaryToken))
    {
        return GetLastError();
    }

    if (!SetTokenInformation(pCall->hPrimaryToken, TokenSessionId, &dwSessionId, sizeof(dwSessionId)))
        return GetLastError();

    return ERROR_SUCCESS;
}


static
DWORD
PrepareChildEnvironment(
    _In_ SECL_REQUEST *pRequest,
    _Inout_ PSECL_TOKEN_CALL pCall)
{
    DWORD dwError;

    /* TODO: LOGON_NETCREDENTIALS_ONLY is ignored */
    if (pRequest->dwLogonFlags & LOGON_WITH_PROFILE)
    {
        dwError = GetTokenUserName(pCall->hPrimaryToken, pCall->szUserName, ARRAYSIZE(pCall->szUserName));
        if (dwError != ERROR_SUCCESS)
            return dwError;

        pCall->ProfileInfo.dwSize = sizeof(pCall->ProfileInfo);
        pCall->ProfileInfo.lpUserName = pCall->szUserName;
        if (!LoadUserProfileW(pCall->hPrimaryToken, &pCall->ProfileInfo))
            return GetLastError();
    }

    if (pRequest->Environment == NULL &&
        !CreateEnvironmentBlock(&pCall->pEnvironment, pCall->hPrimaryToken, FALSE))
    {
        return GetLastError();
    }

    return ERROR_SUCCESS;
}


static
DWORD
CreateChildAttributes(
    _In_ SECL_STARTUPINFO *pStartupInfo,
    _Inout_ PSECL_TOKEN_CALL pCall)
{
    HANDLE ahStd[3];
    SIZE_T cbAttributes = 0;
    DWORD dwError;
    ULONG i, j;

    if (pStartupInfo->dwFlags & STARTF_USESTDHANDLES)
    {
        ahStd[0] = LongToHandle(pStartupInfo->hStdInput);
        ahStd[1] = LongToHandle(pStartupInfo->hStdOutput);
        ahStd[2] = LongToHandle(pStartupInfo->hStdError);
        for (i = 0; i < ARRAYSIZE(ahStd); i++)
        {
            if (!ahStd[i] || ahStd[i] == INVALID_HANDLE_VALUE)
                continue;
            for (j = 0; j < pCall->cInherit && pCall->ahInherit[j] != ahStd[i]; j++)
                ;
            if (j == pCall->cInherit)
                pCall->ahInherit[pCall->cInherit++] = ahStd[i];
        }
    }

    InitializeProcThreadAttributeList(NULL, 2, 0, &cbAttributes);
    pCall->pAttributes = HeapAlloc(GetProcessHeap(), 0, cbAttributes);
    if (pCall->pAttributes == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    if (!InitializeProcThreadAttributeList(pCall->pAttributes, pCall->cInherit ? 2 : 1, 0, &cbAttributes))
    {
        dwError = GetLastError();
        HeapFree(GetProcessHeap(), 0, pCall->pAttributes);
        pCall->pAttributes = NULL;
        return dwError;
    }

    if (!UpdateProcThreadAttribute(pCall->pAttributes,
                                   0,
                                   PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
                                   &pCall->hCallerProcess,
                                   sizeof(pCall->hCallerProcess),
                                   NULL,
                                   NULL) ||
        (pCall->cInherit &&
         !UpdateProcThreadAttribute(pCall->pAttributes,
                                    0,
                                    PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                    pCall->ahInherit,
                                    pCall->cInherit * sizeof(HANDLE),
                                    NULL,
                                    NULL)))
    {
        return GetLastError();
    }

    return ERROR_SUCCESS;
}


static
DWORD
CreateChildForCaller(
    _In_ SECL_REQUEST *pRequest,
    _In_ SECL_STARTUPINFO *pStartupInfo,
    _Inout_ PSECL_TOKEN_CALL pCall,
    _Out_ SECL_RESPONSE *pResponse)
{
    STARTUPINFOEXW StartupInfo;
    HANDLE hProcessForCaller = NULL;
    HANDLE hThreadForCaller = NULL;
    DWORD dwCreationFlags;
    DWORD dwError;

    dwCreationFlags = pRequest->dwCreationFlags | EXTENDED_STARTUPINFO_PRESENT;
    if (pCall->pEnvironment)
        dwCreationFlags |= CREATE_UNICODE_ENVIRONMENT;

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.StartupInfo.lpDesktop = pStartupInfo->Desktop;
    StartupInfo.StartupInfo.lpTitle = pStartupInfo->Title;
    StartupInfo.StartupInfo.dwX = pStartupInfo->dwX;
    StartupInfo.StartupInfo.dwY = pStartupInfo->dwY;
    StartupInfo.StartupInfo.dwXSize = pStartupInfo->dwXSize;
    StartupInfo.StartupInfo.dwYSize = pStartupInfo->dwYSize;
    StartupInfo.StartupInfo.dwXCountChars = pStartupInfo->dwXCountChars;
    StartupInfo.StartupInfo.dwYCountChars = pStartupInfo->dwYCountChars;
    StartupInfo.StartupInfo.dwFillAttribute = pStartupInfo->dwFillAttribute;
    StartupInfo.StartupInfo.dwFlags = pStartupInfo->dwFlags;
    StartupInfo.StartupInfo.wShowWindow = pStartupInfo->wShowWindow;
    StartupInfo.StartupInfo.hStdInput = LongToHandle(pStartupInfo->hStdInput);
    StartupInfo.StartupInfo.hStdOutput = LongToHandle(pStartupInfo->hStdOutput);
    StartupInfo.StartupInfo.hStdError = LongToHandle(pStartupInfo->hStdError);
    StartupInfo.lpAttributeList = pCall->pAttributes;

    if (!CreateProcessAsUserW(pCall->hPrimaryToken,
                              pRequest->ApplicationName,
                              pRequest->CommandLine,
                              NULL,
                              NULL,
                              pCall->cInherit != 0,
                              dwCreationFlags,
                              pCall->pEnvironment ? pCall->pEnvironment : pRequest->Environment,
                              pRequest->CurrentDirectory,
                              &StartupInfo.StartupInfo,
                              &pCall->ProcessInfo))
    {
        return GetLastError();
    }

    if (!DuplicateHandle(GetCurrentProcess(),
                         pCall->ProcessInfo.hProcess,
                         pCall->hCallerProcess,
                         &hProcessForCaller,
                         0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS) ||
        !DuplicateHandle(GetCurrentProcess(),
                         pCall->ProcessInfo.hThread,
                         pCall->hCallerProcess,
                         &hThreadForCaller,
                         0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS))
    {
        dwError = GetLastError();
        TerminateProcess(pCall->ProcessInfo.hProcess, dwError);
        if (hProcessForCaller)
            DuplicateHandle(pCall->hCallerProcess, hProcessForCaller, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
        return dwError;
    }

    pResponse->hProcess = HandleToUlong(hProcessForCaller);
    pResponse->hThread = HandleToUlong(hThreadForCaller);
    pResponse->dwProcessId = pCall->ProcessInfo.dwProcessId;
    pResponse->dwThreadId = pCall->ProcessInfo.dwThreadId;

    if (pCall->ProfileInfo.hProfile != NULL &&
        ReleaseProfileOnExit(pCall->ProcessInfo.hProcess, pCall->hPrimaryToken, pCall->ProfileInfo.hProfile))
    {
        pCall->ProfileInfo.hProfile = NULL;
    }

    return ERROR_SUCCESS;
}


static
VOID
FreeTokenCall(
    _Inout_ PSECL_TOKEN_CALL pCall)
{
    if (pCall->ProcessInfo.hThread)
        CloseHandle(pCall->ProcessInfo.hThread);
    if (pCall->ProcessInfo.hProcess)
        CloseHandle(pCall->ProcessInfo.hProcess);
    if (pCall->pAttributes)
    {
        DeleteProcThreadAttributeList(pCall->pAttributes);
        HeapFree(GetProcessHeap(), 0, pCall->pAttributes);
    }
    if (pCall->pEnvironment)
        DestroyEnvironmentBlock(pCall->pEnvironment);
    if (pCall->ProfileInfo.hProfile != NULL)
        UnloadUserProfile(pCall->hPrimaryToken, pCall->ProfileInfo.hProfile);
    if (pCall->hPrimaryToken)
        CloseHandle(pCall->hPrimaryToken);
    if (pCall->hCallerToken)
        CloseHandle(pCall->hCallerToken);
    if (pCall->hCallerProcess)
        CloseHandle(pCall->hCallerProcess);
}


VOID
__stdcall
SeclCreateProcessWithTokenW(
    _In_ handle_t hBinding,
    _In_ DWORD hToken,
    _In_ SECL_REQUEST *pRequest,
    _In_ SECL_STARTUPINFO *pStartupInfo,
    _Out_ SECL_RESPONSE *pResponse)
{
    SECL_TOKEN_CALL Call;
    DWORD dwError;

    TRACE("SeclCreateProcessWithTokenW(%p %lx %p %p %p)\n", hBinding, hToken, pRequest, pStartupInfo, pResponse);

    ZeroMemory(&Call, sizeof(Call));

    dwError = CheckTokenRequest(hBinding, pRequest, &Call);
    if (dwError == ERROR_SUCCESS)
        dwError = GetCallerPrimaryToken(hToken, &Call);
    if (dwError == ERROR_SUCCESS)
        dwError = PrepareChildEnvironment(pRequest, &Call);
    if (dwError == ERROR_SUCCESS)
        dwError = CreateChildAttributes(pStartupInfo, &Call);
    if (dwError == ERROR_SUCCESS)
        dwError = CreateChildForCaller(pRequest, pStartupInfo, &Call, pResponse);

    FreeTokenCall(&Call);

    pResponse->dwError = dwError;
}
