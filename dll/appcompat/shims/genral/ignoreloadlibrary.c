/*
 * PROJECT:     ReactOS 'Layers' Shim library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IgnoreLoadLibrary shim
 * COPYRIGHT:   Copyright 2019 Mark Jansen <mark.jansen@reactos.org>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <shimlib.h>
#include "ntndk.h"

typedef HMODULE(WINAPI* LOADLIBRARYAPROC)(LPCSTR lpLibFileName);
typedef HMODULE(WINAPI* LOADLIBRARYEXAPROC)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
typedef HMODULE(WINAPI* LOADLIBRARYWPROC)(LPCWSTR lpLibFileName);
typedef HMODULE(WINAPI* LOADLIBRARYEXWPROC)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);


#define SHIM_NS         IgnoreLoadLibrary
#include <setup_shim.inl>


static BOOL SHIM_OBJ_NAME(Find)(PCSTR NameA, PCWSTR NameW, HMODULE* Module)
{
    PCSTR Entry = SHIM_OBJ_NAME(g_szCommandLine);
    SIZE_T Start = 0, Length = 0, n;

    if (!Entry || (!NameA && !NameW))
        return FALSE;

    for (n = 0; NameA ? NameA[n] : NameW[n]; n++)
    {
        WCHAR ch = NameA ? (WCHAR)(UCHAR)NameA[n] : NameW[n];
        if (ch == '\\' || ch == '/')
            Start = n + 1;
    }
    Length = n - Start;

    while (*Entry)
    {
        PCSTR End = Entry, Colon = NULL;
        SIZE_T EntryLength;

        while (*End && *End != ';')
        {
            if (*End == ':')
                Colon = End;
            End++;
        }
        EntryLength = (Colon ? Colon : End) - Entry;

        if (EntryLength == Length)
        {
            for (n = 0; n < Length; n++)
            {
                WCHAR ch1 = (WCHAR)(UCHAR)Entry[n];
                WCHAR ch2 = NameA ? (WCHAR)(UCHAR)NameA[Start + n] : NameW[Start + n];

                if (ch1 >= 'A' && ch1 <= 'Z') ch1 += 'a' - 'A';
                if (ch2 >= 'A' && ch2 <= 'Z') ch2 += 'a' - 'A';
                if (ch1 != ch2)
                    break;
            }
            if (n == Length)
            {
                ULONG_PTR Value = 0;

                if (Colon)
                {
                    for (Colon++; Colon < End && *Colon >= '0' && *Colon <= '9'; Colon++)
                        Value = Value * 10 + (*Colon - '0');
                }
                *Module = (HMODULE)Value;
                return TRUE;
            }
        }
        Entry = *End ? End + 1 : End;
    }
    return FALSE;
}


HMODULE WINAPI SHIM_OBJ_NAME(APIHook_LoadLibraryA)(LPCSTR lpLibFileName)
{
    HMODULE Module;
    DWORD dwOldErrorMode;

    if (SHIM_OBJ_NAME(Find)(lpLibFileName, NULL, &Module))
        return Module;

    dwOldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    Module = CALL_SHIM(0, LOADLIBRARYAPROC)(lpLibFileName);
    SetErrorMode(dwOldErrorMode);

    return Module;
}

HMODULE WINAPI SHIM_OBJ_NAME(APIHook_LoadLibraryExA)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    HMODULE Module;
    DWORD dwOldErrorMode;

    if (SHIM_OBJ_NAME(Find)(lpLibFileName, NULL, &Module))
        return Module;

    dwOldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    Module = CALL_SHIM(1, LOADLIBRARYEXAPROC)(lpLibFileName, hFile, dwFlags);
    SetErrorMode(dwOldErrorMode);

    return Module;
}

HMODULE WINAPI SHIM_OBJ_NAME(APIHook_LoadLibraryW)(LPCWSTR lpLibFileName)
{
    HMODULE Module;
    DWORD dwOldErrorMode;

    if (SHIM_OBJ_NAME(Find)(NULL, lpLibFileName, &Module))
        return Module;

    dwOldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    Module = CALL_SHIM(2, LOADLIBRARYWPROC)(lpLibFileName);
    SetErrorMode(dwOldErrorMode);

    return Module;
}

HMODULE WINAPI SHIM_OBJ_NAME(APIHook_LoadLibraryExW)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    HMODULE Module;
    DWORD dwOldErrorMode;

    if (SHIM_OBJ_NAME(Find)(NULL, lpLibFileName, &Module))
        return Module;

    dwOldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    Module = CALL_SHIM(3, LOADLIBRARYEXWPROC)(lpLibFileName, hFile, dwFlags);
    SetErrorMode(dwOldErrorMode);

    return Module;
}


#define SHIM_NUM_HOOKS  4
#define SHIM_SETUP_HOOKS \
    SHIM_HOOK(0, "KERNEL32.DLL", "LoadLibraryA", SHIM_OBJ_NAME(APIHook_LoadLibraryA)) \
    SHIM_HOOK(1, "KERNEL32.DLL", "LoadLibraryExA", SHIM_OBJ_NAME(APIHook_LoadLibraryExA)) \
    SHIM_HOOK(2, "KERNEL32.DLL", "LoadLibraryW", SHIM_OBJ_NAME(APIHook_LoadLibraryW)) \
    SHIM_HOOK(3, "KERNEL32.DLL", "LoadLibraryExW", SHIM_OBJ_NAME(APIHook_LoadLibraryExW))

#include <implement_shim.inl>
