/*
 * ReactOS rundll32
 * Copyright (C) 2003-2004 ReactOS Team
 *
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS rundll32.exe
 * FILE:            base/system/rundll32/rundll32.c
 * PURPOSE:         Run a DLL as a program
 * PROGRAMMER:      ShadowFlare (blakflare@hotmail.com)
 */

// Both UNICODE and _UNICODE must be either defined or undefined
// because some headers use UNICODE and others use _UNICODE
#ifdef UNICODE
#ifndef _UNICODE
#define _UNICODE
#endif
#else
#ifdef _UNICODE
#define UNICODE
#endif
#endif

#define WIN32_NO_STATUS
#include <stdarg.h>
#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winuser.h>
#include <tchar.h>
#include <undocuser.h> // For WM_POPUPSYSTEMMENU

#include "resource.h"

typedef int (WINAPI *DllWinMainW)(
  HWND hWnd,
  HINSTANCE hInstance,
  LPWSTR lpwCmdLine,
  int nCmdShow
);
typedef int (WINAPI *DllWinMainA)(
  HWND hWnd,
  HINSTANCE hInstance,
  LPSTR lpCmdLine,
  int nCmdShow
);

/*
LPCTSTR DllNotLoaded = _T("LoadLibrary failed to load \"%s\"");
LPCTSTR MissingEntry = _T("Missing entry point:%s\nIn %s");
*/
LPCTSTR rundll32_wtitle = _T("rundll32");
LPCTSTR rundll32_wclass = _T("RunDLL");

TCHAR ModuleFileName[MAX_PATH+1];
LPTSTR ModuleTitle;


// CommandLineToArgv converts a command-line string to argc and
// argv similar to the ones in the standard main function.
// This is a specialized version coded specifically for rundll32
// and is not intended to be used in any other program.
LPTSTR *WINAPI CommandLineToArgv(LPCTSTR lpCmdLine, int *lpArgc)
{
    LPTSTR *argv, lpSrc, lpDest, lpArg;
    int argc, nBSlash, nNames;
    BOOL bInQuotes, bFirstChar;

    // If null was passed in for lpCmdLine, there are no arguments
    if (!lpCmdLine) {
        if (lpArgc)
            *lpArgc = 0;
        return 0;
    }

    lpSrc = (LPTSTR)lpCmdLine;
    // Skip spaces at beginning
    while (*lpSrc == _T(' ') || *lpSrc == _T('\t'))
        lpSrc++;

    // If command-line starts with null, there are no arguments
    if (*lpSrc == 0) {
        if (lpArgc)
            *lpArgc = 0;
        return 0;
    }

    lpArg = lpSrc;
    argc = 0;
    nBSlash = 0;
    bInQuotes = FALSE;
    bFirstChar = TRUE;
    nNames = 0;

    // Count the number of arguments
    while (nNames < 4) {
        if (*lpSrc == 0 || (*lpSrc == _T(',') && nNames == 2) || ((*lpSrc == _T(' ') || *lpSrc == _T('\t')) && !bInQuotes)) {
            // Whitespace not enclosed in quotes signals the start of another argument
            argc++;

            // Skip whitespace between arguments
            while (*lpSrc == _T(' ') || *lpSrc == _T('\t') || (*lpSrc == _T(',') && nNames == 2))
                lpSrc++;
            if (*lpSrc == 0)
                break;
            if (nNames >= 3) {
                // Increment the count for the last argument
                argc++;
                break;
            }
            nBSlash = 0;
            bFirstChar = TRUE;
            continue;
        }
        else if (*lpSrc == _T('\\')) {
            // Count consecutive backslashes
            nBSlash++;
            bFirstChar = FALSE;
        }
        else if (*lpSrc == _T('\"') && !(nBSlash & 1)) {
            // Open or close quotes
            bInQuotes = !bInQuotes;
            nBSlash = 0;
        }
        else {
            // Some other character
            nBSlash = 0;
            if (bFirstChar && ((*lpSrc != _T('/') && nNames <= 1) || nNames > 1))
                nNames++;
            bFirstChar = FALSE;
        }
        lpSrc++;
    }

    // Allocate space for the pointers in argv and the strings in one block
    argv = (LPTSTR *)malloc(argc * sizeof(LPTSTR) + (_tcslen(lpArg) + 1) * sizeof(TCHAR));

    if (!argv) {
        // Memory allocation failed
        if (lpArgc)
            *lpArgc = 0;
        return 0;
    }

    lpSrc = lpArg;
    lpDest = lpArg = (LPTSTR)(argv + argc);
    argc = 0;
    nBSlash = 0;
    bInQuotes = FALSE;
    bFirstChar = TRUE;
    nNames = 0;

    // Fill the argument array
    while (nNames < 4) {
        if (*lpSrc == 0 || (*lpSrc == _T(',') && nNames == 2) || ((*lpSrc == _T(' ') || *lpSrc == _T('\t')) && !bInQuotes)) {
            // Whitespace not enclosed in quotes signals the start of another argument
            // Null-terminate argument
            *lpDest++ = 0;
            argv[argc++] = lpArg;

            // Skip whitespace between arguments
            while (*lpSrc == _T(' ') || *lpSrc == _T('\t') || (*lpSrc == _T(',') && nNames == 2))
                lpSrc++;
            if (*lpSrc == 0)
                break;
            lpArg = lpDest;
            if (nNames >= 3) {
                // Copy the rest of the command-line to the last argument
                argv[argc++] = lpArg;
                _tcscpy(lpArg,lpSrc);
                break;
            }
            nBSlash = 0;
            bFirstChar = TRUE;
            continue;
        }
        else if (*lpSrc == _T('\\')) {
            *lpDest++ = _T('\\');
            lpSrc++;

            // Count consecutive backslashes
            nBSlash++;
            bFirstChar = FALSE;
        }
        else if (*lpSrc == _T('\"')) {
            if (!(nBSlash & 1)) {
                // If an even number of backslashes are before the quotes,
                // the quotes don't go in the output
                lpDest -= nBSlash / 2;
                bInQuotes = !bInQuotes;
            }
            else {
                // If an odd number of backslashes are before the quotes,
                // output a quote
                lpDest -= (nBSlash + 1) / 2;
                *lpDest++ = _T('\"');
                bFirstChar = FALSE;
            }
            lpSrc++;
            nBSlash = 0;
        }
        else {
            // Copy other characters
            if (bFirstChar && ((*lpSrc != _T('/') && nNames <= 1) || nNames > 1))
                nNames++;
            *lpDest++ = *lpSrc++;
            nBSlash = 0;
            bFirstChar = FALSE;
        }
    }

    if (lpArgc)
        *lpArgc = argc;
    return argv;
}

void GetModuleTitle(void)
{
    LPTSTR lpStr;

    GetModuleFileName(0,ModuleFileName,MAX_PATH);
    ModuleTitle = ModuleFileName;

    for (lpStr = ModuleFileName;*lpStr;lpStr++) {
        if (*lpStr == _T('\\'))
            ModuleTitle = lpStr+1;
    }

    for (lpStr = ModuleTitle;*lpStr;lpStr++) {
        if (_tcsicmp(lpStr,_T(".exe"))==0)
            break;
    }

    *lpStr = 0;
}

// The macro ConvertToWideChar takes a tstring parameter and returns
// a pointer to a unicode string.  A conversion is performed if
// necessary.  FreeConvertedWideChar string should be used on the
// return value of ConvertToWideChar when the string is no longer
// needed.  The original string or the string that is returned
// should not be modified until FreeConvertedWideChar has been called.
#ifdef UNICODE
#define ConvertToWideChar(lptString) (lptString)
#define FreeConvertedWideChar(lpwString)
#else

LPWSTR ConvertToWideChar(LPCSTR lpString)
{
    LPWSTR lpwString;
    size_t nStrLen;

    nStrLen = strlen(lpString) + 1;

    lpwString = (LPWSTR)malloc(nStrLen * sizeof(WCHAR));
    MultiByteToWideChar(0,0,lpString,nStrLen,lpwString,nStrLen);

    return lpwString;
}

#define FreeConvertedWideChar(lpwString) free(lpwString)
#endif

// The macro ConvertToMultiByte takes a tstring parameter and returns
// a pointer to an ansi string.  A conversion is performed if
// necessary.  FreeConvertedMultiByte string should be used on the
// return value of ConvertToMultiByte when the string is no longer
// needed.  The original string or the string that is returned
// should not be modified until FreeConvertedMultiByte has been called.
#ifdef UNICODE
#define ConvertToMultiByte(lptString) DuplicateToMultiByte(lptString,0)
#define FreeConvertedMultiByte(lpaString) free(lpaString)
#else
#define ConvertToMultiByte(lptString) (lptString)
#define FreeConvertedMultiByte(lpaString)
#endif

// DuplicateToMultiByte takes a tstring parameter and always returns
// a pointer to a duplicate ansi string.  If nBufferSize is zero,
// the buffer length is the exact size of the string plus the
// terminating null.  If nBufferSize is nonzero, the buffer length
// is equal to nBufferSize.  As with strdup, free should be called
// for the returned string when it is no longer needed.
LPSTR DuplicateToMultiByte(LPCTSTR lptString, size_t nBufferSize)
{
    LPSTR lpString;
    size_t nStrLen;

    nStrLen = _tcslen(lptString) + 1;
    if (nBufferSize == 0) nBufferSize = nStrLen;

    lpString = (LPSTR)malloc(nBufferSize);
#ifdef UNICODE
    WideCharToMultiByte(0,0,lptString,nStrLen,lpString,nBufferSize,0,0);
#else
    strncpy(lpString,lptString,nBufferSize);
#endif

    return lpString;
}

typedef struct
{
    HWND hwndOwner;
    HWND hwndTarget;
} FIND_OWNED, *PFIND_OWNED;

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    PFIND_OWNED pFindOwned = (PFIND_OWNED)lParam;
    if (pFindOwned->hwndOwner == GetWindow(hwnd, GW_OWNER))
    {
        pFindOwned->hwndTarget = hwnd;
        return FALSE;
    }
    return TRUE;
}

LRESULT CALLBACK EmptyWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_POPUPSYSTEMMENU:
        case WM_SYSCOMMAND:
        {
            /* Find the owned window */
            FIND_OWNED FindOwned = { hWnd, NULL };
            EnumWindows(EnumWindowsProc, (LPARAM)&FindOwned);
            /* Forward message */
            if (FindOwned.hwndTarget)
                PostMessageW(FindOwned.hwndTarget, uMsg, wParam, lParam);
            break;
        }
        case WM_ACTIVATE:
        {
            /* Find the owned window */
            FIND_OWNED FindOwned = { hWnd, NULL };
            EnumWindows(EnumWindowsProc, (LPARAM)&FindOwned);
            if (FindOwned.hwndTarget)
            {
                if (LOWORD(wParam) != WA_INACTIVE) /* To be activated */
                {
                    SetActiveWindow(FindOwned.hwndTarget);
                    return 0;
                }
            }
            /* Fall through */
        }
        default:
            return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

// Registers a minimal window class for passing to the dll function
BOOL RegisterBlankClass(HINSTANCE hInstance, HINSTANCE hPrevInstance)
{
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style         = 0;
    wcex.lpfnWndProc   = EmptyWindowProc;
    wcex.cbClsExtra    = 0;
    wcex.cbWndExtra    = 0;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = 0;
    wcex.hCursor       = 0;
    wcex.hbrBackground = 0;
    wcex.lpszMenuName  = 0;
    wcex.lpszClassName = rundll32_wclass;
    wcex.hIconSm       = 0;

    return (RegisterClassEx(&wcex) != (ATOM)0);
}

static LPCWSTR
SkipProgramName(LPCWSTR p)
{
    if (*p == L'"')
    {
        for (p++; *p != UNICODE_NULL && *p != L'"'; p++);
        if (*p == L'"')
            p++;
    }
    else
    {
        while (*p != UNICODE_NULL && *p != L' ' && *p != L'\t')
            p++;
    }
    while (*p == L' ' || *p == L'\t')
        p++;
    return p;
}

static BOOL
IsDllFor32BitMachine(LPCWSTR lpDllName)
{
    MEMORY_BASIC_INFORMATION Info;
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_NT_HEADERS NtHeaders;
    HMODULE hImage;
    SIZE_T cbMapped;
    BOOL bResult = FALSE;

    hImage = LoadLibraryExW(lpDllName, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (hImage == NULL)
        return FALSE;
    DosHeader = (PIMAGE_DOS_HEADER)((ULONG_PTR)hImage & ~(ULONG_PTR)3);
    if (VirtualQuery(DosHeader, &Info, sizeof(Info)) &&
        DosHeader->e_magic == IMAGE_DOS_SIGNATURE && DosHeader->e_lfanew > 0)
    {
        cbMapped = Info.RegionSize - (SIZE_T)((PBYTE)DosHeader - (PBYTE)Info.BaseAddress);
        if ((SIZE_T)DosHeader->e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) <= cbMapped)
        {
            NtHeaders = (PIMAGE_NT_HEADERS)((PBYTE)DosHeader + DosHeader->e_lfanew);
            bResult = NtHeaders->Signature == IMAGE_NT_SIGNATURE &&
                      (NtHeaders->FileHeader.Characteristics & IMAGE_FILE_32BIT_MACHINE);
        }
    }
    FreeLibrary(hImage);
    return bResult;
}

/* A 64-bit rundll32 hands a DLL built for a 32-bit machine to the rundll32 of the WoW64 directory and waits for it. */
static BOOL
RunWow64Rundll32(LPCWSTR lpDllName, int nCmdShow)
{
    WCHAR szRundll32[MAX_PATH];
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFOW StartupInfo;
    LPCWSTR lpArguments;
    LPWSTR lpCommandLine;
    SIZE_T cchCommandLine;
    UINT cchDirectory;
    BOOL bResult;

    if (sizeof(void *) < 8 || GetLastError() != ERROR_BAD_EXE_FORMAT || !IsDllFor32BitMachine(lpDllName))
        return FALSE;
    cchDirectory = GetSystemWow64DirectoryW(szRundll32, _countof(szRundll32));
    if (cchDirectory == 0 || cchDirectory + sizeof("\\rundll32.exe") > _countof(szRundll32))
        return FALSE;
    wcscat(szRundll32, L"\\rundll32.exe");

    lpArguments = SkipProgramName(GetCommandLineW());
    cchCommandLine = wcslen(szRundll32) + wcslen(lpArguments) + 4;
    lpCommandLine = malloc(cchCommandLine * sizeof(WCHAR));
    if (lpCommandLine == NULL)
        return FALSE;
    _snwprintf(lpCommandLine, cchCommandLine, L"\"%s\" %s", szRundll32, lpArguments);
    lpCommandLine[cchCommandLine - 1] = UNICODE_NULL;

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    StartupInfo.wShowWindow = (WORD)nCmdShow;
    bResult = CreateProcessW(szRundll32, lpCommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo);
    free(lpCommandLine);
    if (!bResult)
        return FALSE;
    CloseHandle(ProcessInfo.hThread);
    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
    CloseHandle(ProcessInfo.hProcess);
    return TRUE;
}

int WINAPI _tWinMain(
  HINSTANCE hInstance,
  HINSTANCE hPrevInstance,
  LPTSTR lpCmdLine,
  int nCmdShow
)
{
    int argc;
    TCHAR szMsg[RC_STRING_MAX_SIZE];

    LPTSTR *argv;
    LPTSTR lptCmdLine,lptDllName,lptFuncName,lptMsgBuffer;
    WCHAR ResolvedFile[MAX_PATH + 1] = {0}, *lpManifestName;
    LPSTR lpFuncName,lpaCmdLine;
    LPWSTR lpwCmdLine;
    HMODULE hDll;
    DllWinMainW fnDllWinMainW;
    DllWinMainA fnDllWinMainA;
    HWND hWindow;
    int i;
    size_t nStrLen;

    ACTCTXW ActCtx = {sizeof(ACTCTX), ACTCTX_FLAG_RESOURCE_NAME_VALID};
    HANDLE hActCtx;
    ULONG_PTR cookie;
    BOOL bActivated;

    // Get command-line in argc-argv format
    argv = CommandLineToArgv(GetCommandLine(),&argc);

    // Skip all beginning arguments starting with a slash (/)
    for (i = 1; i < argc; i++)
        if (*argv[i] != _T('/')) break;

    // If no dll was specified, there is nothing to do
    if (i >= argc) {
        if (argv) free(argv);
        return 0;
    }

    lptDllName = argv[i++];

    // The next argument, which specifies the name of the dll function,
    // can either have a comma between it and the dll filename or a space.
    // Using a comma here is the preferred method
    if (i < argc)
        lptFuncName = argv[i++];
    else
        lptFuncName = _T("");

    // If no function name was specified, nothing needs to be done
    if (!*lptFuncName) {
        if (argv) free(argv);
        return 0;
    }

    // The rest of the arguments will be passed to dll function
    if (i < argc)
        lptCmdLine = argv[i];
    else
        lptCmdLine = _T("");

    lpManifestName = lptDllName;
    if (GetFileAttributesW(lptDllName) == INVALID_FILE_ATTRIBUTES)
    {
        LPWSTR FilePart = NULL;
        if (SearchPathW(NULL, lptDllName, NULL, _countof(ResolvedFile) - 1, ResolvedFile, &FilePart))
        {
            lpManifestName = ResolvedFile;
        }
    }

    // FIXME: If there is a .manifest file next to the input file, we should use that instead!
    ActCtx.lpSource = lpManifestName;
    ActCtx.lpResourceName = (LPCWSTR)123;
    hActCtx = CreateActCtx(&ActCtx);
    bActivated = (hActCtx != INVALID_HANDLE_VALUE ? ActivateActCtx(hActCtx, &cookie) : FALSE);

    // Everything is all setup, so load the dll now
    hDll = LoadLibrary(lptDllName);
    if (hDll) {
        nStrLen = _tcslen(lptFuncName);
        // Make a non-unicode version of the function name,
        // since that is all GetProcAddress accepts
        lpFuncName = DuplicateToMultiByte(lptFuncName,nStrLen + 2);

#ifdef UNICODE
        lpFuncName[nStrLen] = 'W';
        lpFuncName[nStrLen+1] = 0;
        // Get address of unicode version of the dll function if it exists
        fnDllWinMainW = (DllWinMainW)GetProcAddress(hDll,lpFuncName);
        fnDllWinMainA = 0;
        if (!fnDllWinMainW) {
            // If no unicode function was found, get the address of the non-unicode function
            lpFuncName[nStrLen] = 'A';
            fnDllWinMainA = (DllWinMainA)GetProcAddress(hDll,lpFuncName);
            if (!fnDllWinMainA) {
                // If first non-unicode function was not found, get the address
                // of the other non-unicode function
                lpFuncName[nStrLen] = 0;
                fnDllWinMainA = (DllWinMainA)GetProcAddress(hDll,lpFuncName);
            }
        }
#else
        // Get address of non-unicode version of the dll function if it exists
        fnDllWinMainA = (DllWinMainA)GetProcAddress(hDll,lpFuncName);
        fnDllWinMainW = 0;
        if (!fnDllWinMainA) {
            // If first non-unicode function was not found, get the address
            // of the other non-unicode function
            lpFuncName[nStrLen] = 'A';
            lpFuncName[nStrLen+1] = 0;
            fnDllWinMainA = (DllWinMainA)GetProcAddress(hDll,lpFuncName);
            if (!fnDllWinMainA) {
                // If non-unicode function was not found, get the address of the unicode function
                lpFuncName[nStrLen] = 'W';
                fnDllWinMainW = (DllWinMainW)GetProcAddress(hDll,lpFuncName);
            }
        }
#endif

        free(lpFuncName);

        if (!RegisterBlankClass(hInstance, hPrevInstance))
        {
            FreeLibrary(hDll);
            if (bActivated)
                DeactivateActCtx(0, cookie);
            return 0;
        }
        // Create a window so we can pass a window handle to
        // the dll function; this is required
        hWindow = CreateWindowEx(0,rundll32_wclass,rundll32_wtitle,0,CW_USEDEFAULT,0,CW_USEDEFAULT,0,0,0,hInstance,0);

        if (fnDllWinMainW) {
            // Convert the command-line string to unicode and call the dll function
            lpwCmdLine = ConvertToWideChar(lptCmdLine);
            fnDllWinMainW(hWindow,hInstance,lpwCmdLine,nCmdShow);
            FreeConvertedWideChar(lpwCmdLine);
        }
        else if (fnDllWinMainA) {
            // Convert the command-line string to ansi and call the dll function
            lpaCmdLine = ConvertToMultiByte(lptCmdLine);
            fnDllWinMainA(hWindow,hInstance,lpaCmdLine,nCmdShow);
            FreeConvertedMultiByte(lpaCmdLine);
        }
        else {
            // The specified dll function was not found; display an error message
            GetModuleTitle();
            LoadString( GetModuleHandle(NULL), IDS_MissingEntry, (LPTSTR) szMsg,RC_STRING_MAX_SIZE);

            lptMsgBuffer = (LPTSTR)malloc((_tcslen(szMsg) - 4 + _tcslen(lptFuncName) + _tcslen(lptDllName) + 1) * sizeof(TCHAR));
            _stprintf(lptMsgBuffer,szMsg,lptFuncName,lptDllName);
            MessageBox(0,lptMsgBuffer,ModuleTitle,MB_ICONERROR);
            free(lptMsgBuffer);
        }

        DestroyWindow(hWindow);
        UnregisterClass(rundll32_wclass,hInstance);

        // The dll function has finished executing, so unload it
        FreeLibrary(hDll);
    }
    else if (!RunWow64Rundll32(lptDllName, nCmdShow)) {
        // The dll could not be loaded; display an error message
        GetModuleTitle();
        LoadString( GetModuleHandle(NULL), IDS_DllNotLoaded, (LPTSTR) szMsg,RC_STRING_MAX_SIZE);

        lptMsgBuffer = (LPTSTR)malloc((_tcslen(szMsg) - 2 + _tcslen(lptDllName) + 1) * sizeof(TCHAR));
        _stprintf(lptMsgBuffer,szMsg,lptDllName);

        MessageBox(0,lptMsgBuffer,ModuleTitle,MB_ICONERROR);
        free(lptMsgBuffer);
    }

    if (bActivated)
        DeactivateActCtx(0, cookie);

    if (argv) free(argv);
    return 0; /* rundll32 always returns 0! */
}

