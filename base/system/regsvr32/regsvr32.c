/*
 * ReactOS regsvr32
 * Copyright (C) 2004-2006 ReactOS Team
 *
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS regsvr32.exe
 * FILE:            base/system/regsvr32/regsvr32.c
 * PURPOSE:         Register a COM component in the registry
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
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <ole2.h>
#include <tchar.h>

typedef HRESULT (WINAPI *DLLREGISTER)(void);
typedef HRESULT (WINAPI *DLLINSTALL)(BOOL bInstall, LPWSTR lpwCmdLine);

#define EXITCODE_SUCCESS 0
#define EXITCODE_PARAMERROR 1
#define EXITCODE_LOADERROR 3
#define EXITCODE_NOENTRY 4
#define EXITCODE_FAILURE 5

LPCSTR szDllRegister = "DllRegisterServer";
LPCSTR szDllUnregister = "DllUnregisterServer";
LPCSTR szDllInstall = "DllInstall";
#ifdef UNICODE
LPCWSTR tszDllRegister = L"DllRegisterServer";
LPCWSTR tszDllUnregister = L"DllUnregisterServer";
LPCWSTR tszDllInstall = L"DllInstall";
#else
#define tszDllRegister szDllRegister
#define tszDllUnregister szDllUnregister
#define tszDllInstall szDllInstall
#endif

#include "resource.h"

LPCTSTR ModuleTitle = _T("RegSvr32");

TCHAR UsageMessage[RC_STRING_MAX_SIZE];
TCHAR NoDllSpecified[RC_STRING_MAX_SIZE];
TCHAR InvalidFlag[RC_STRING_MAX_SIZE];
TCHAR SwitchN_NoI[RC_STRING_MAX_SIZE];
TCHAR DllNotLoaded[RC_STRING_MAX_SIZE];
TCHAR MissingEntry[RC_STRING_MAX_SIZE];
TCHAR FailureMessage[RC_STRING_MAX_SIZE];
TCHAR SuccessMessage[RC_STRING_MAX_SIZE];

// The macro CommandLineToArgv maps to a function that converts
// a command-line string to argc and argv similar to the ones
// in the standard main function.  If this code is compiled for
// unicode, the build-in Windows API function is used, otherwise
// a non-unicode non-API version is used for compatibility with
// Windows versions that have no unicode support.
#ifdef UNICODE
#define CommandLineToArgv CommandLineToArgvW
#include <shellapi.h>
#else
#define CommandLineToArgv CommandLineToArgvT

LPTSTR *WINAPI CommandLineToArgvT(LPCTSTR lpCmdLine, int *lpArgc)
{
	HGLOBAL hargv;
	LPTSTR *argv, lpSrc, lpDest, lpArg;
	int argc, nBSlash;
	BOOL bInQuotes;

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

	// Count the number of arguments
	while (1) {
		if (*lpSrc == 0 || ((*lpSrc == _T(' ') || *lpSrc == _T('\t')) && !bInQuotes)) {
			// Whitespace not enclosed in quotes signals the start of another argument
			argc++;

			// Skip whitespace between arguments
			while (*lpSrc == _T(' ') || *lpSrc == _T('\t'))
				lpSrc++;
			if (*lpSrc == 0)
				break;
			nBSlash = 0;
			continue;
		}
		else if (*lpSrc == _T('\\')) {
			// Count consecutive backslashes
			nBSlash++;
		}
		else if (*lpSrc == _T('\"') && !(nBSlash & 1)) {
			// Open or close quotes
			bInQuotes = !bInQuotes;
			nBSlash = 0;
		}
		else {
			// Some other character
			nBSlash = 0;
		}
		lpSrc++;
	}

	// Allocate space the same way as CommandLineToArgvW for compatibility
	hargv = GlobalAlloc(0, argc * sizeof(LPTSTR) + (_tcslen(lpArg) + 1) * sizeof(TCHAR));
	argv = (LPTSTR *)GlobalLock(hargv);

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

	// Fill the argument array
	while (1) {
		if (*lpSrc == 0 || ((*lpSrc == _T(' ') || *lpSrc == _T('\t')) && !bInQuotes)) {
			// Whitespace not enclosed in quotes signals the start of another argument
			// Null-terminate argument
			*lpDest++ = 0;
			argv[argc++] = lpArg;

			// Skip whitespace between arguments
			while (*lpSrc == _T(' ') || *lpSrc == _T('\t'))
				lpSrc++;
			if (*lpSrc == 0)
				break;
			lpArg = lpDest;
			nBSlash = 0;
			continue;
		}
		else if (*lpSrc == _T('\\')) {
			*lpDest++ = _T('\\');
			lpSrc++;

			// Count consecutive backslashes
			nBSlash++;
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
			}
			lpSrc++;
			nBSlash = 0;
		}
		else {
			// Copy other characters
			*lpDest++ = *lpSrc++;
			nBSlash = 0;
		}
	}

	if (lpArgc)
		*lpArgc = argc;
	return argv;
}

#endif

// The macro ConvertToWideChar takes a tstring parameter and returns
// a pointer to a unicode string.  A conversion is performed if
// necessary.  FreeConvertedWideChar string should be used on the
// return value of ConvertToWideChar when the string is no longer
// needed.  The original string or the string that is returned
// should not be modified until FreeConvertedWideChar has been called.
#ifdef UNICODE
#define ConvertToWideChar(lptString) (lptString)
#define FreeConvertedWideChar(lpwString) ((void) 0)
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

void DisplayMessage(BOOL bConsole, BOOL bSilent, LPCTSTR lpMessage, LPCTSTR lpTitle, UINT uType)
{
	if (!bSilent)
		MessageBox(0,lpMessage,lpTitle,uType);
	if (bConsole)
		_tprintf(_T("%s: %s\n\n"),lpTitle,lpMessage);
}

static LPCWSTR SkipProgramName(LPCWSTR lpCommandLine)
{
	LPCWSTR p = lpCommandLine;

	if (*p == L'"')
	{
		for (p++; *p != UNICODE_NULL && *p != L'"'; p++);
		if (*p == L'"')
			p++;
		return p;
	}
	while (*p != UNICODE_NULL && *p != L' ' && *p != L'\t')
		p++;
	return p;
}

static BOOL GetDllBitness(LPCWSTR lpDllName, BOOL *pb32Bit)
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
			bResult = NtHeaders->Signature == IMAGE_NT_SIGNATURE;
			*pb32Bit = (NtHeaders->FileHeader.Characteristics & IMAGE_FILE_32BIT_MACHINE) != 0;
		}
	}
	FreeLibrary(hImage);
	return bResult;
}

/* A regsvr32 gives a DLL of the other bitness to the WoW64 or the native regsvr32 with the arguments after its own program name, verbatim. */
static BOOL RunOtherRegSvr32(LPCWSTR lpDllName, DWORD dwLoadError, int *pnExitCode)
{
	WCHAR szRegSvr32[MAX_PATH];
	PROCESS_INFORMATION ProcessInfo;
	STARTUPINFOW StartupInfo;
	LPWSTR lpArguments;
	DWORD dwExitCode;
	UINT cchDirectory;
	BOOL bResult, b32Bit = FALSE, bWow64 = FALSE;

	if (dwLoadError != ERROR_BAD_EXE_FORMAT || !GetDllBitness(lpDllName, &b32Bit))
		return FALSE;
	if (sizeof(void *) < 8)
	{
		if (b32Bit || !IsWow64Process(GetCurrentProcess(), &bWow64) || !bWow64)
			return FALSE;
		cchDirectory = GetWindowsDirectoryW(szRegSvr32, _countof(szRegSvr32));
		if (cchDirectory == 0 || cchDirectory + sizeof("\\Sysnative\\regsvr32.exe") > _countof(szRegSvr32))
			return FALSE;
		wcscat(szRegSvr32, L"\\Sysnative");
	}
	else
	{
		if (!b32Bit)
			return FALSE;
		cchDirectory = GetSystemWow64DirectoryW(szRegSvr32, _countof(szRegSvr32));
		if (cchDirectory == 0 || cchDirectory + sizeof("\\regsvr32.exe") > _countof(szRegSvr32))
			return FALSE;
	}
	wcscat(szRegSvr32, L"\\regsvr32.exe");

	lpArguments = _wcsdup(SkipProgramName(GetCommandLineW()));
	if (lpArguments == NULL)
		return FALSE;
	ZeroMemory(&StartupInfo, sizeof(StartupInfo));
	StartupInfo.cb = sizeof(StartupInfo);
	bResult = CreateProcessW(szRegSvr32, lpArguments, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo);
	free(lpArguments);
	if (!bResult)
		return FALSE;
	CloseHandle(ProcessInfo.hThread);
	WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
	if (!GetExitCodeProcess(ProcessInfo.hProcess, &dwExitCode))
		dwExitCode = EXITCODE_LOADERROR;
	CloseHandle(ProcessInfo.hProcess);
	*pnExitCode = (int)dwExitCode;
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
	LPTSTR *argv;
	LPTSTR lptDllName,lptDllCmdLine,lptMsgBuffer;
	LPCTSTR lptFuncName;
	LPCSTR lpFuncName;
	LPWSTR lpwDllCmdLine;
	BOOL bUnregister,bSilent,bConsole,bInstall,bNoRegister;
	UINT nDllCount, fuOldErrorMode;
	HMODULE hDll;
	DLLREGISTER fnDllRegister;
	DLLINSTALL fnDllInstall;
	HRESULT hResult;
	DWORD dwErr;
	int nRetValue,i;

	// Get Langues msg
	LoadString( GetModuleHandle(NULL), IDS_UsageMessage, (LPTSTR) UsageMessage,RC_STRING_MAX_SIZE);
	LoadString( GetModuleHandle(NULL), IDS_NoDllSpecified, (LPTSTR) NoDllSpecified,RC_STRING_MAX_SIZE);
	LoadString( GetModuleHandle(NULL), IDS_InvalidFlag, (LPTSTR) InvalidFlag,RC_STRING_MAX_SIZE);
	LoadString( GetModuleHandle(NULL), IDS_SwitchN_NoI, (LPTSTR) SwitchN_NoI,RC_STRING_MAX_SIZE);

	LoadString( GetModuleHandle(NULL), IDS_DllNotLoaded, (LPTSTR)   DllNotLoaded,RC_STRING_MAX_SIZE);
	LoadString( GetModuleHandle(NULL), IDS_MissingEntry, (LPTSTR)   MissingEntry,RC_STRING_MAX_SIZE);
	LoadString( GetModuleHandle(NULL), IDS_FailureMessage, (LPTSTR) FailureMessage,RC_STRING_MAX_SIZE);
	LoadString( GetModuleHandle(NULL), IDS_SuccessMessage, (LPTSTR) SuccessMessage,RC_STRING_MAX_SIZE);

	// Get command-line in argc-argv format
	argv = CommandLineToArgv(GetCommandLine(),&argc);

	// Initialize variables
	lptFuncName = 0;
	lptDllCmdLine = 0;
	nDllCount = 0;
	bUnregister = FALSE;
	bSilent = FALSE;
	bConsole = FALSE;
	bInstall = FALSE;
	bNoRegister = FALSE;

	// Find all arguments starting with a slash (/)
	for (i = 1; i < argc; i++) {
		if (*argv[i] == _T('/') || *argv[i] == '-') {
			switch (argv[i][1]) {
			case _T('u'):
			case _T('U'):
				bUnregister = TRUE;
				break;
			case _T('s'):
			case _T('S'):
				bSilent = TRUE;
				break;
			case _T('c'):
			case _T('C'):
				bConsole = TRUE;
				break;
			case _T('i'):
			case _T('I'):
				bInstall = TRUE;
				lptDllCmdLine = argv[i];
				while (*lptDllCmdLine != 0 && *lptDllCmdLine != _T(':'))
					lptDllCmdLine++;
				if (*lptDllCmdLine == _T(':'))
					lptDllCmdLine++;
				break;
			case _T('n'):
			case _T('N'):
				bNoRegister = TRUE;
				break;
			default:
				if (!lptFuncName)
					lptFuncName = argv[i];
			}
		}
		else {
			nDllCount++;
		}
	}

	// An unrecognized flag was used, display a message and show available options

	if (lptFuncName) {
		lptMsgBuffer = (LPTSTR)malloc((_tcslen(UsageMessage) - 2 + _tcslen(InvalidFlag) - 2 + _tcslen(lptFuncName) + 1) * sizeof(TCHAR));
		_stprintf(lptMsgBuffer + (_tcslen(UsageMessage) - 2),InvalidFlag,lptFuncName);
		_stprintf(lptMsgBuffer,UsageMessage,lptMsgBuffer + (_tcslen(UsageMessage) - 2));
		DisplayMessage(bConsole,bSilent,lptMsgBuffer,ModuleTitle,MB_ICONEXCLAMATION);
		free(lptMsgBuffer);
		GlobalFree(argv);
		return EXITCODE_PARAMERROR;
	}

	// /n was used without /i, display a message and show available options
	if (bNoRegister && (!bInstall)) {
		lptMsgBuffer = (LPTSTR)malloc((_tcslen(UsageMessage) - 2 + _tcslen(SwitchN_NoI) + 1) * sizeof(TCHAR));
		_stprintf(lptMsgBuffer,UsageMessage,SwitchN_NoI);
		DisplayMessage(bConsole,bSilent,lptMsgBuffer,ModuleTitle,MB_ICONEXCLAMATION);
		free(lptMsgBuffer);
		GlobalFree(argv);
		return EXITCODE_PARAMERROR;
	}

	// No dll was specified, display a message and show available options
	if (nDllCount == 0) {
		lptMsgBuffer = (LPTSTR)malloc((_tcslen(UsageMessage) - 2 + _tcslen(NoDllSpecified) + 1) * sizeof(TCHAR));
		_stprintf(lptMsgBuffer,UsageMessage,NoDllSpecified);
		DisplayMessage(bConsole,bSilent,lptMsgBuffer,ModuleTitle,MB_ICONEXCLAMATION);
		free(lptMsgBuffer);
		GlobalFree(argv);
		return EXITCODE_PARAMERROR;
	}

	nRetValue = EXITCODE_SUCCESS;
	if (!bUnregister) {
		lpFuncName = szDllRegister;
		lptFuncName = tszDllRegister;
	}
	else {
		lpFuncName = szDllUnregister;
		lptFuncName = tszDllUnregister;
	}

	if (lptDllCmdLine)
		lpwDllCmdLine = ConvertToWideChar(lptDllCmdLine);
	else
		lpwDllCmdLine = 0;

	// Initialize OLE32 before attempting to register the
	// dll.  Some dll's require this to register properly
	OleInitialize(0);

	// (Un)register every dll whose filename was passed in the command-line string
	for (i = 1; i < argc && nRetValue == EXITCODE_SUCCESS; i++) {
		// Arguments that do not start with a slash (/) are filenames
		if (*argv[i] != _T('/')) {
			lptDllName = argv[i];

			fuOldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);
			// Everything is all setup, so load the dll now
			hDll = LoadLibraryEx(lptDllName,0,LOAD_WITH_ALTERED_SEARCH_PATH);
			if (hDll) {
				SetErrorMode(fuOldErrorMode);
				if (!bNoRegister) {
					// Get the address of DllRegisterServer or DllUnregisterServer
					fnDllRegister = (DLLREGISTER)GetProcAddress(hDll,lpFuncName);
					if (fnDllRegister) {
						// If the function exists, call it
						hResult = fnDllRegister();
						if (hResult == S_OK) {
							// (Un)register succeeded, display a message
							lptMsgBuffer = (LPTSTR)malloc((_tcslen(SuccessMessage) - 4 + _tcslen(lptFuncName) + _tcslen(lptDllName) + 1) * sizeof(TCHAR));
							_stprintf(lptMsgBuffer,SuccessMessage,lptFuncName,lptDllName);
							DisplayMessage(bConsole,bSilent,lptMsgBuffer,ModuleTitle,MB_ICONINFORMATION);
						}
						else {
							// (Un)register failed, display a message
							lptMsgBuffer = (LPTSTR)malloc((_tcslen(FailureMessage) + _tcslen(lptFuncName) + _tcslen(lptDllName) + 1) * sizeof(TCHAR));
							_stprintf(lptMsgBuffer,FailureMessage,lptFuncName,lptDllName,hResult);
							DisplayMessage(bConsole,bSilent,lptMsgBuffer,ModuleTitle,MB_ICONEXCLAMATION);
						}
						free(lptMsgBuffer);
						if (hResult != S_OK)
							nRetValue = EXITCODE_FAILURE;
					}
					else {
						FreeLibrary(hDll);
						// Dll(Un)register was not found, display an error message
						lptMsgBuffer = (LPTSTR)malloc((_tcslen(MissingEntry) - 8 + _tcslen(lptFuncName) * 2 + _tcslen(lptDllName) * 2 + 1) * sizeof(TCHAR));
						_stprintf(lptMsgBuffer,MissingEntry,lptDllName,lptFuncName,lptFuncName,lptDllName);
						DisplayMessage(bConsole,bSilent,lptMsgBuffer,ModuleTitle,MB_ICONEXCLAMATION);
						free(lptMsgBuffer);
						nRetValue = EXITCODE_NOENTRY;
					}
				}

				if (bInstall && nRetValue == EXITCODE_SUCCESS) {
					// Get the address of DllInstall
					fnDllInstall = (DLLINSTALL)GetProcAddress(hDll,szDllInstall);
					if (fnDllInstall) {
						// If the function exists, call it
						if (!bUnregister)
							hResult = fnDllInstall(1,lpwDllCmdLine);
						else
							hResult = fnDllInstall(0,lpwDllCmdLine);
						if (hResult == S_OK) {
							// (Un)install succeeded, display a message
							lptMsgBuffer = (LPTSTR)malloc((_tcslen(SuccessMessage) - 4 + _tcslen(tszDllInstall) + _tcslen(lptDllName) + 1) * sizeof(TCHAR));
							_stprintf(lptMsgBuffer,SuccessMessage,tszDllInstall,lptDllName);
							DisplayMessage(bConsole,bSilent,lptMsgBuffer,ModuleTitle,MB_ICONINFORMATION);
						}
						else {
							// (Un)install failed, display a message
							lptMsgBuffer = (LPTSTR)malloc((_tcslen(FailureMessage) + _tcslen(tszDllInstall) + _tcslen(lptDllName) + 1) * sizeof(TCHAR));
							_stprintf(lptMsgBuffer,FailureMessage,tszDllInstall,lptDllName,hResult);
							DisplayMessage(bConsole,bSilent,lptMsgBuffer,ModuleTitle,MB_ICONEXCLAMATION);
						}
						free(lptMsgBuffer);
						if (hResult != S_OK)
							nRetValue = EXITCODE_FAILURE;
					}
					else {
						// DllInstall was not found, display an error message
						lptMsgBuffer = (LPTSTR)malloc((_tcslen(MissingEntry) - 8 + _tcslen(tszDllInstall) * 2 + _tcslen(lptDllName) * 2 + 1) * sizeof(TCHAR));
						_stprintf(lptMsgBuffer,MissingEntry,lptDllName,tszDllInstall,tszDllInstall,lptDllName);
						DisplayMessage(bConsole,bSilent,lptMsgBuffer,ModuleTitle,MB_ICONEXCLAMATION);
						free(lptMsgBuffer);
						nRetValue = EXITCODE_NOENTRY;
					}
				}

				// The dll function has finished executing, so unload it
				FreeLibrary(hDll);
			}
			else {
				// The dll could not be loaded; display an error message
				dwErr = GetLastError();
				SetErrorMode(fuOldErrorMode);
				if (RunOtherRegSvr32(lptDllName, dwErr, &nRetValue))
					break;
				lptMsgBuffer = (LPTSTR)malloc((_tcslen(DllNotLoaded) + 2 + _tcslen(lptDllName) + 1) * sizeof(TCHAR));
				_stprintf(lptMsgBuffer,DllNotLoaded,lptDllName,dwErr);
				DisplayMessage(bConsole,bSilent,lptMsgBuffer,ModuleTitle,MB_ICONEXCLAMATION);
				free(lptMsgBuffer);
				nRetValue = EXITCODE_LOADERROR;
			}
		}
	}

	if (lpwDllCmdLine)
		FreeConvertedWideChar(lpwDllCmdLine);
	GlobalFree(argv);
	OleUninitialize();
	return nRetValue;
}
