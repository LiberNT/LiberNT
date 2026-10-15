/*
 * PROJECT:     LiberNT Terminal Services Utility Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     User name lookup with a per-process cache
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <lmcons.h>

#include "resource.h"

typedef struct _USER_CACHE_ENTRY
{
    struct _USER_CACHE_ENTRY *Next;
    PSID Sid;
    WCHAR UserName[UNLEN + 1];
} USER_CACHE_ENTRY, *PUSER_CACHE_ENTRY;

static HINSTANCE UtilDllInstance;
static CRITICAL_SECTION UserCacheLock;
static PUSER_CACHE_ENTRY UserCache;

static BOOL
FindCachedUser(PSID Sid, PWSTR UserName)
{
    PUSER_CACHE_ENTRY Entry;
    BOOL Found = FALSE;

    EnterCriticalSection(&UserCacheLock);
    for (Entry = UserCache; Entry; Entry = Entry->Next)
    {
        if (EqualSid(Entry->Sid, Sid))
        {
            lstrcpyW(UserName, Entry->UserName);
            Found = TRUE;
            break;
        }
    }
    LeaveCriticalSection(&UserCacheLock);
    return Found;
}

static VOID
CacheUser(PSID Sid, PCWSTR UserName)
{
    DWORD SidLength = GetLengthSid(Sid);
    PUSER_CACHE_ENTRY Entry;

    Entry = HeapAlloc(GetProcessHeap(), 0, sizeof(*Entry) + SidLength);
    if (!Entry)
        return;

    Entry->Sid = (PSID)(Entry + 1);
    CopySid(SidLength, Entry->Sid, Sid);
    lstrcpynW(Entry->UserName, UserName, ARRAYSIZE(Entry->UserName));

    EnterCriticalSection(&UserCacheLock);
    Entry->Next = UserCache;
    UserCache = Entry;
    LeaveCriticalSection(&UserCacheLock);
}

static BOOL
LookupUser(PSID Sid, PWSTR UserName)
{
    WCHAR DomainName[DNLEN + 1];
    DWORD UserNameLength = UNLEN + 1;
    DWORD DomainNameLength = ARRAYSIZE(DomainName);
    SID_NAME_USE Use;

    if (FindCachedUser(Sid, UserName))
        return TRUE;

    if (!LookupAccountSidW(NULL, Sid, UserName, &UserNameLength, DomainName, &DomainNameLength, &Use))
        return FALSE;

    CacheUser(Sid, UserName);
    return TRUE;
}

static VOID
ReturnUserName(LPWSTR Buffer, PULONG Length, PCWSTR UserName)
{
    ULONG Available = *Length;
    INT Copied;

    if (!Available)
        return;

    if (UserName)
    {
        if (Available < 2)
            return;
        lstrcpynW(Buffer, UserName, Available - 1);
        Copied = lstrlenW(Buffer);
    }
    else
    {
        Copied = LoadStringW(UtilDllInstance, IDS_UNKNOWN_USER, Buffer, Available - 1);
    }

    if (Copied <= 0 || Available < 2)
        return;

    Buffer[Available - 1] = UNICODE_NULL;
    *Length = lstrlenW(Buffer);
}

VOID
WINAPI
CachedGetUserFromSid(PSID pSid, LPWSTR pUserName, PULONG pcwcUserName)
{
    WCHAR UserName[UNLEN + 1];

    if (pSid && IsValidSid(pSid) && LookupUser(pSid, UserName))
        ReturnUserName(pUserName, pcwcUserName, UserName);
    else
        ReturnUserName(pUserName, pcwcUserName, NULL);
}

BOOL
WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    PUSER_CACHE_ENTRY Entry;

    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            UtilDllInstance = hinstDLL;
            DisableThreadLibraryCalls(hinstDLL);
            InitializeCriticalSection(&UserCacheLock);
            break;

        case DLL_PROCESS_DETACH:
            if (lpvReserved)
                break;
            while ((Entry = UserCache))
            {
                UserCache = Entry->Next;
                HeapFree(GetProcessHeap(), 0, Entry);
            }
            DeleteCriticalSection(&UserCacheLock);
            break;
    }

    return TRUE;
}
