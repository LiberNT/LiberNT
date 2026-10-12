/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS System Libraries
 * FILE:            dll/win32/kernel32/include/console.h
 * PURPOSE:         Console API Client Definitions
 * PROGRAMMERS:     Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

#pragma once

/* CONSTANTS ******************************************************************/

#define CONSOLE_HANDLE_ALLOC            ((HANDLE)(LONG_PTR)-1)
#define CONSOLE_HANDLE_ALLOC_NO_WINDOW  ((HANDLE)(LONG_PTR)-2)

typedef struct _BASE_PSEUDO_CONSOLE
{
    HANDLE Signal;
    HANDLE Reference;
    HANDLE Process;
} BASE_PSEUDO_CONSOLE, *PBASE_PSEUDO_CONSOLE;


/* FUNCTION PROTOTYPES ********************************************************/

BOOLEAN
WINAPI
ConDllInitialize(IN ULONG Reason,
                 IN PWSTR SessionDir);

BOOL WINAPI
GetConsoleHandleInformation(IN HANDLE hHandle,
                            OUT LPDWORD lpdwFlags);

BOOL WINAPI
SetConsoleHandleInformation(IN HANDLE hHandle,
                            IN DWORD dwMask,
                            IN DWORD dwFlags);

HANDLE
TranslateStdHandle(HANDLE hHandle);

LPCWSTR
IntCheckForConsoleFileName(IN LPCWSTR pszName,
                           IN DWORD dwDesiredAccess);

/* EOF */
