/*
 * PROJECT:     ReactOS Service Control Manager
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/system/services/lock.c
 * PURPOSE:     User-side Services Start Serialization Lock functions
 * COPYRIGHT:   Copyright 2012 Hermès Bélusca
 */

/* INCLUDES *****************************************************************/

#include "services.h"

#include <time.h>
#include <winnls.h>
#include <lmcons.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* The unique user service start lock of the SCM */
static PSTART_LOCK pServiceStartLock = NULL;


/* FUNCTIONS *****************************************************************/

static
BOOL
ScmGetClientAccountName(OUT LPWSTR Owner,
                        IN DWORD OwnerLength)
{
    WCHAR Name[UNLEN + 1], Domain[MAX_PATH], Computer[MAX_COMPUTERNAME_LENGTH + 1];
    BYTE Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    PTOKEN_USER User = (PTOKEN_USER)Buffer;
    DWORD NameLength = RTL_NUMBER_OF(Name);
    DWORD DomainLength = RTL_NUMBER_OF(Domain);
    DWORD ComputerLength = RTL_NUMBER_OF(Computer);
    DWORD Length;
    SID_NAME_USE Use;
    HANDLE Token;
    BOOL Success;

    if (RpcImpersonateClient(NULL) != RPC_S_OK)
        return FALSE;

    Success = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &Token);
    RpcRevertToSelf();
    if (!Success)
        return FALSE;

    Success = GetTokenInformation(Token, TokenUser, User, sizeof(Buffer), &Length);
    CloseHandle(Token);
    if (!Success)
        return FALSE;

    if (!LookupAccountSidW(NULL,
                           User->User.Sid,
                           Name,
                           &NameLength,
                           Domain,
                           &DomainLength,
                           &Use))
    {
        return FALSE;
    }

    if (GetComputerNameW(Computer, &ComputerLength) && _wcsicmp(Domain, Computer) == 0)
        wcscpy(Domain, L".");

    if (wcslen(Domain) + wcslen(Name) + 2 > OwnerLength)
        return FALSE;

    wcscpy(Owner, Domain);
    wcscat(Owner, L"\\");
    wcscat(Owner, Name);
    return TRUE;
}

/*
 * NOTE: IsServiceController is TRUE if locked by the
 * Service Control Manager, and FALSE otherwise.
 */
DWORD
ScmAcquireServiceStartLock(IN BOOL IsServiceController,
                           OUT LPSC_RPC_LOCK lpLock)
{
    WCHAR Owner[UNLEN + MAX_PATH + 2];
    DWORD dwRequiredSize;
    DWORD dwError = ERROR_SUCCESS;

    *lpLock = NULL;
    Owner[0] = UNICODE_NULL;

    if (!IsServiceController && !ScmGetClientAccountName(Owner, RTL_NUMBER_OF(Owner)))
        Owner[0] = UNICODE_NULL;

    /* Lock the service database exclusively */
    ScmLockDatabaseExclusive();

    if (pServiceStartLock != NULL)
    {
        dwError = ERROR_SERVICE_DATABASE_LOCKED;
        goto done;
    }

    /* Allocate a new lock for the database */
    dwRequiredSize = FIELD_OFFSET(START_LOCK, LockOwner) +
                     (DWORD)(wcslen(Owner) + 1) * sizeof(WCHAR);

    pServiceStartLock = HeapAlloc(GetProcessHeap(),
                                  HEAP_ZERO_MEMORY,
                                  dwRequiredSize);
    if (pServiceStartLock == NULL)
    {
        dwError = ERROR_NOT_ENOUGH_MEMORY;
        goto done;
    }

    pServiceStartLock->Tag = LOCK_TAG;
    pServiceStartLock->TimeWhenLocked = (DWORD)time(NULL);
    wcscpy(pServiceStartLock->LockOwner, Owner);

    *lpLock = (LPSC_RPC_LOCK)pServiceStartLock;

done:
    /* Unlock the service database */
    ScmUnlockDatabase();

    return dwError;
}


DWORD
ScmReleaseServiceStartLock(IN OUT LPSC_RPC_LOCK lpLock)
{
    PSTART_LOCK pStartLock;
    DWORD dwError = ERROR_SUCCESS;

    if (lpLock == NULL)
        return ERROR_INVALID_SERVICE_LOCK;

    pStartLock = (PSTART_LOCK)*lpLock;

    if (pStartLock->Tag != LOCK_TAG)
        return ERROR_INVALID_SERVICE_LOCK;

    /* Lock the service database exclusively */
    ScmLockDatabaseExclusive();

    /* Release the lock handle */
    if ((pStartLock == pServiceStartLock) &&
        (pServiceStartLock != NULL))
    {
        HeapFree(GetProcessHeap(), 0, pServiceStartLock);
        pServiceStartLock = NULL;
        *lpLock = NULL;

        dwError = ERROR_SUCCESS;
    }
    else
    {
        dwError = ERROR_INVALID_SERVICE_LOCK;
    }

    /* Unlock the service database */
    ScmUnlockDatabase();

    return dwError;
}


/*
 * Helper functions for RQueryServiceLockStatusW() and
 * RQueryServiceLockStatusA().
 */
DWORD
ScmQueryServiceLockStatusW(OUT LPQUERY_SERVICE_LOCK_STATUSW lpLockStatus,
                           IN DWORD cbBufSize,
                           OUT LPDWORD pcbBytesNeeded)
{
    LPCWSTR Owner = L"";
    DWORD dwRequiredSize;
    DWORD dwError = ERROR_SUCCESS;

    /* Lock the service database shared */
    ScmLockDatabaseShared();

    if (pServiceStartLock != NULL)
        Owner = pServiceStartLock->LockOwner;

    dwRequiredSize = sizeof(QUERY_SERVICE_LOCK_STATUSW) +
                     (DWORD)(wcslen(Owner) + 1) * sizeof(WCHAR);
    *pcbBytesNeeded = dwRequiredSize;

    if (cbBufSize < dwRequiredSize)
    {
        dwError = ERROR_INSUFFICIENT_BUFFER;
        goto done;
    }

    wcscpy((LPWSTR)(lpLockStatus + 1), Owner);
    lpLockStatus->lpLockOwner = (LPWSTR)(ULONG_PTR)sizeof(QUERY_SERVICE_LOCK_STATUSW);

    if (pServiceStartLock != NULL)
    {
        lpLockStatus->fIsLocked = TRUE;
        lpLockStatus->dwLockDuration = (DWORD)time(NULL) - pServiceStartLock->TimeWhenLocked;
    }
    else
    {
        lpLockStatus->fIsLocked = FALSE;
        lpLockStatus->dwLockDuration = 0;
    }

done:
    /* Unlock the service database */
    ScmUnlockDatabase();

    return dwError;
}


DWORD
ScmQueryServiceLockStatusA(OUT LPQUERY_SERVICE_LOCK_STATUSA lpLockStatus,
                           IN DWORD cbBufSize,
                           OUT LPDWORD pcbBytesNeeded)
{
    LPCWSTR Owner = L"";
    DWORD dwRequiredSize;
    DWORD dwOwnerSize;
    DWORD dwError = ERROR_SUCCESS;

    /* Lock the service database shared */
    ScmLockDatabaseShared();

    if (pServiceStartLock != NULL)
        Owner = pServiceStartLock->LockOwner;

    dwOwnerSize = WideCharToMultiByte(CP_ACP, 0, Owner, -1, NULL, 0, NULL, NULL);
    if (dwOwnerSize == 0)
    {
        Owner = L"";
        dwOwnerSize = sizeof(CHAR);
    }

    dwRequiredSize = sizeof(QUERY_SERVICE_LOCK_STATUSA) +
                     (DWORD)(wcslen(Owner) + 1) * sizeof(WCHAR);
    *pcbBytesNeeded = dwRequiredSize;

    if (cbBufSize < dwRequiredSize)
    {
        dwError = ERROR_INSUFFICIENT_BUFFER;
        goto done;
    }

    WideCharToMultiByte(CP_ACP,
                        0,
                        Owner,
                        -1,
                        (LPSTR)(lpLockStatus + 1),
                        dwOwnerSize,
                        NULL,
                        NULL);
    lpLockStatus->lpLockOwner = (LPSTR)(ULONG_PTR)sizeof(QUERY_SERVICE_LOCK_STATUSA);

    if (pServiceStartLock != NULL)
    {
        lpLockStatus->fIsLocked = TRUE;
        lpLockStatus->dwLockDuration = (DWORD)time(NULL) - pServiceStartLock->TimeWhenLocked;
    }
    else
    {
        lpLockStatus->fIsLocked = FALSE;
        lpLockStatus->dwLockDuration = 0;
    }

done:
    /* Unlock the service database */
    ScmUnlockDatabase();

    return dwError;
}

/* EOF */
