/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RegisterDlls self-registration of 32-bit and native DLLs through SetupInstallFromInfSection
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <apitest.h>
#include <winuser.h>
#include <winreg.h>
#include <setupapi.h>
#include <strsafe.h>

typedef struct _REGISTER_RESULT
{
    UINT Starts;
    UINT Calls;
    UINT Others;
    UINT OtherNotification;
    UINT FailureCode;
    UINT Win32Error;
    WCHAR FileName[MAX_PATH];
} REGISTER_RESULT;

static const char RegisterInf[] =
    "[Version]\r\n"
    "Signature=\"$Windows NT$\"\r\n"
    "\r\n"
    "[Wow64]\r\n"
    "RegisterDlls=Wow64.Dlls\r\n"
    "\r\n"
    "[Wow64.Dlls]\r\n"
    "10,SysWOW64,scrrun.dll,1\r\n"
    "\r\n"
    "[Native]\r\n"
    "RegisterDlls=Native.Dlls\r\n"
    "\r\n"
    "[Native.Dlls]\r\n"
    "11,,scrrun.dll,1\r\n";

static UINT CALLBACK RegisterCallback(PVOID Context, UINT Notification, UINT_PTR Param1, UINT_PTR Param2)
{
    REGISTER_RESULT *Result = Context;
    SP_REGISTER_CONTROL_STATUSW *Status = (SP_REGISTER_CONTROL_STATUSW *)Param1;

    if (Notification == 0x00000019)
    {
        Result->Starts++;
    }
    else if (Notification == 0x00000020)
    {
        Result->Calls++;
        Result->FailureCode = Status->FailureCode;
        Result->Win32Error = Status->Win32Error;
        StringCchCopyW(Result->FileName, ARRAYSIZE(Result->FileName), Status->FileName ? Status->FileName : L"");
    }
    else
    {
        Result->Others++;
        Result->OtherNotification = Notification;
    }
    return FILEOP_DOIT;
}

static void TestSection(HINF Inf, PCWSTR Section, PCWSTR ExpectedDirectory)
{
    REGISTER_RESULT Result = { 0 };
    WCHAR Expected[MAX_PATH];
    BOOL Ok;

    StringCchPrintfW(Expected, ARRAYSIZE(Expected), L"%ls\\scrrun.dll", ExpectedDirectory);
    Ok = SetupInstallFromInfSectionW(NULL, Inf, Section, SPINST_REGSVR | SPINST_REGISTERCALLBACKAWARE, NULL, NULL, 0,
                                     RegisterCallback, &Result, NULL, NULL);
    ok(Ok, "%ls: SetupInstallFromInfSectionW failed %lu\n", Section, GetLastError());
    ok(Result.Starts == 1, "%ls: %u SPFILENOTIFY_STARTREGISTRATION (0x19) notifications\n", Section, Result.Starts);
    ok(Result.Calls == 1, "%ls: %u SPFILENOTIFY_ENDREGISTRATION (0x20) notifications\n", Section, Result.Calls);
    ok(Result.Others == 0, "%ls: %u other notifications, last 0x%x\n", Section, Result.Others, Result.OtherNotification);
    ok(Result.FailureCode == SPREG_SUCCESS, "%ls: FailureCode %u Win32Error 0x%x\n",
       Section, Result.FailureCode, Result.Win32Error);
    ok(Result.Win32Error == ERROR_SUCCESS, "%ls: Win32Error 0x%x\n", Section, Result.Win32Error);
    ok(!_wcsicmp(Result.FileName, Expected), "%ls: FileName '%ls', expected '%ls'\n",
       Section, Result.FileName, Expected);
}

START_TEST(SetupInstallRegisterDlls)
{
    WCHAR Wow64Directory[MAX_PATH], SystemDirectory[MAX_PATH], Wow64Dll[MAX_PATH];
    WCHAR TempDirectory[MAX_PATH], InfPath[MAX_PATH];
    HANDLE File;
    DWORD Written = 0;
    UINT ErrorLine = 0;
    HINF Inf;

    if (sizeof(void *) != 8)
    {
        skip("RegisterDlls of 32-bit DLLs from a 64-bit process needs a 64-bit test process\n");
        return;
    }
    if (!GetSystemWow64DirectoryW(Wow64Directory, ARRAYSIZE(Wow64Directory)) ||
        !GetSystemDirectoryW(SystemDirectory, ARRAYSIZE(SystemDirectory)))
    {
        skip("No WOW64 system directory: %lu\n", GetLastError());
        return;
    }
    StringCchPrintfW(Wow64Dll, ARRAYSIZE(Wow64Dll), L"%ls\\scrrun.dll", Wow64Directory);
    if (GetFileAttributesW(Wow64Dll) == INVALID_FILE_ATTRIBUTES)
    {
        skip("%ls is missing\n", Wow64Dll);
        return;
    }

    GetTempPathW(ARRAYSIZE(TempDirectory), TempDirectory);
    StringCchPrintfW(InfPath, ARRAYSIZE(InfPath), L"%lsregdlls_%lu.inf", TempDirectory, GetCurrentProcessId());
    File = CreateFileW(InfPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(File != INVALID_HANDLE_VALUE, "Cannot create %ls: %lu\n", InfPath, GetLastError());
    if (File == INVALID_HANDLE_VALUE)
        return;
    ok(WriteFile(File, RegisterInf, sizeof(RegisterInf) - 1, &Written, NULL), "WriteFile failed %lu\n", GetLastError());
    CloseHandle(File);

    Inf = SetupOpenInfFileW(InfPath, NULL, INF_STYLE_WIN4, &ErrorLine);
    ok(Inf != INVALID_HANDLE_VALUE, "SetupOpenInfFileW failed %lu at line %u\n", GetLastError(), ErrorLine);
    if (Inf != INVALID_HANDLE_VALUE)
    {
        TestSection(Inf, L"Wow64", Wow64Directory);
        TestSection(Inf, L"Native", SystemDirectory);
        SetupCloseInfFile(Inf);
    }
    DeleteFileW(InfPath);
}
