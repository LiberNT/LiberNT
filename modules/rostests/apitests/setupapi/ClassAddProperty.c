/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the AddProperty directive of a class install section
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <winreg.h>
#include <winuser.h>
#include <setupapi.h>
#include <initguid.h>
#include <devpropdef.h>
#include <cfgmgr32.h>
#include <strsafe.h>

DEFINE_GUID(TEST_CLASS_GUID, 0x5d9d1c2e, 0x6f0b, 0x4e1a, 0x9c, 0x3e, 0x27, 0x41, 0xa8, 0x0b, 0x6d, 0x15);
DEFINE_GUID(TEST_PROPERTY_SET, 0x8a3c4f11, 0x2b6e, 0x4d7a, 0xb0, 0x5c, 0x91, 0x13, 0xe2, 0x7f, 0x40, 0x6a);

#define CLASS_GUID_TEXT "{5d9d1c2e-6f0b-4e1a-9c3e-2741a80b6d15}"
#define SET_TEXT "{8a3c4f11-2b6e-4d7a-b05c-9113e27f406a}"

static const char FirstInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=LntPropertyTestClass\r\n"
    "ClassGuid=" CLASS_GUID_TEXT "\r\n"
    "[ClassInstall32]\r\n"
    "AddReg=Class.AddReg\r\n"
    "AddProperty=Class.Properties\r\n"
    "[Class.AddReg]\r\n"
    "HKR,,,0,\"LiberNT property test class\"\r\n"
    "[Class.Properties]\r\n"
    SET_TEXT ",2,0x11,,1\r\n"
    SET_TEXT ",3,0x11,,0\r\n"
    SET_TEXT ",4,0x7,,0x12345670\r\n"
    SET_TEXT ",5,0x12,,\"first text\"\r\n"
    SET_TEXT ",6,0x2012,,\"alpha\",\"beta\"\r\n"
    SET_TEXT ",7,0x1003,,01,02,ff\r\n"
    SET_TEXT ",8,0x7,,0xf0\r\n";

static const char SecondInf[] =
    "[Version]\r\n"
    "Signature=\"$WINDOWS NT$\"\r\n"
    "Class=LntPropertyTestClass\r\n"
    "ClassGuid=" CLASS_GUID_TEXT "\r\n"
    "[ClassInstall32]\r\n"
    "AddProperty=Class.Properties\r\n"
    "[Class.Properties]\r\n"
    SET_TEXT ",4,0x7,0x00000008,0x0000000f\r\n"
    SET_TEXT ",5,0x12,0x00000001,\"second text\"\r\n"
    SET_TEXT ",6,0x2012,0x00000004,\"beta\",\"gamma\"\r\n"
    SET_TEXT ",8,0x7,0x00000010,0x3c\r\n"
    SET_TEXT ",9,0x7,0x00000002,0x99\r\n";

static
BOOL
InstallInf(
    _In_ PCSTR Text,
    _Out_ PDWORD Error)
{
    CHAR Directory[MAX_PATH], Path[MAX_PATH];
    HANDLE File;
    DWORD Written;
    BOOL Result;

    GetTempPathA(ARRAYSIZE(Directory), Directory);
    StringCbPrintfA(Path, sizeof(Path), "%slntaddprop.inf", Directory);
    File = CreateFileA(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileA failed: %lu\n", GetLastError());
    if (File == INVALID_HANDLE_VALUE)
    {
        *Error = GetLastError();
        return FALSE;
    }
    WriteFile(File, Text, lstrlenA(Text), &Written, NULL);
    CloseHandle(File);

    SetLastError(0xdeadbeef);
    Result = SetupDiInstallClassA(NULL, Path, DI_QUIETINSTALL, NULL);
    *Error = Result ? ERROR_SUCCESS : GetLastError();
    DeleteFileA(Path);
    return Result;
}

static
VOID
CheckProperty(
    _In_ ULONG Pid,
    _In_ DEVPROPTYPE ExpectedType,
    _In_reads_bytes_(ExpectedSize) const VOID *Expected,
    _In_ ULONG ExpectedSize)
{
    DEVPROPKEY Key = { TEST_PROPERTY_SET, Pid };
    BYTE Buffer[256];
    ULONG Size = sizeof(Buffer);
    DEVPROPTYPE Type = 0xdeadbeef;
    CONFIGRET Cr;

    Cr = CM_Get_Class_PropertyW(&TEST_CLASS_GUID, &Key, &Type, Buffer, &Size, 0);
    ok(Cr == CR_SUCCESS, "pid %lu: CM_Get_Class_PropertyW returned %lu\n", Pid, Cr);
    if (Cr != CR_SUCCESS)
        return;
    ok(Type == ExpectedType, "pid %lu: type 0x%lx, expected 0x%lx\n", Pid, Type, ExpectedType);
    ok(Size == ExpectedSize, "pid %lu: size %lu, expected %lu\n", Pid, Size, ExpectedSize);
    ok(Size == ExpectedSize && !memcmp(Buffer, Expected, Size), "pid %lu: unexpected data\n", Pid);
}

static
VOID
CheckMissing(
    _In_ ULONG Pid)
{
    DEVPROPKEY Key = { TEST_PROPERTY_SET, Pid };
    BYTE Buffer[16];
    ULONG Size = sizeof(Buffer);
    DEVPROPTYPE Type;
    CONFIGRET Cr;

    Cr = CM_Get_Class_PropertyW(&TEST_CLASS_GUID, &Key, &Type, Buffer, &Size, 0);
    ok(Cr == CR_NO_SUCH_VALUE, "pid %lu: CM_Get_Class_PropertyW returned %lu, expected CR_NO_SUCH_VALUE\n", Pid, Cr);
}

static
ULONG
CountTestKeys(VOID)
{
    DEVPROPKEY Keys[64];
    ULONG Count = ARRAYSIZE(Keys), Index, Found = 0;

    if (CM_Get_Class_Property_Keys(&TEST_CLASS_GUID, Keys, &Count, 0) != CR_SUCCESS)
        return 0;
    for (Index = 0; Index < Count; Index++)
    {
        if (IsEqualGUID(&Keys[Index].fmtid, &TEST_PROPERTY_SET))
            Found++;
    }
    return Found;
}

START_TEST(ClassAddProperty)
{
    static const DEVPROP_BOOLEAN True = DEVPROP_TRUE, False = DEVPROP_FALSE;
    static const ULONG First4 = 0x12345670, Second4 = 0x1234567f, First8 = 0xf0, Second8 = 0x30;
    static const WCHAR FirstText[] = L"first text";
    static const WCHAR FirstList[] = L"alpha\0beta\0";
    static const WCHAR SecondList[] = L"alpha\0beta\0gamma\0";
    static const BYTE Binary[] = { 0x01, 0x02, 0xff };
    HKEY ClassKey;
    DWORD Error;

    if (!InstallInf(FirstInf, &Error))
    {
        if (Error == ERROR_ACCESS_DENIED)
            skip("Installing a setup class needs administrator rights\n");
        else
            ok(0, "SetupDiInstallClassA failed: %lu\n", Error);
        goto Cleanup;
    }

    CheckProperty(2, DEVPROP_TYPE_BOOLEAN, &True, sizeof(True));
    CheckProperty(3, DEVPROP_TYPE_BOOLEAN, &False, sizeof(False));
    CheckProperty(4, DEVPROP_TYPE_UINT32, &First4, sizeof(First4));
    CheckProperty(5, DEVPROP_TYPE_STRING, FirstText, sizeof(FirstText));
    CheckProperty(6, DEVPROP_TYPE_STRING_LIST, FirstList, sizeof(FirstList));
    CheckProperty(7, DEVPROP_TYPE_BINARY, Binary, sizeof(Binary));
    CheckProperty(8, DEVPROP_TYPE_UINT32, &First8, sizeof(First8));
    CheckMissing(9);
    ok(CountTestKeys() == 7, "CM_Get_Class_Property_Keys lists %lu test properties, expected 7\n", CountTestKeys());

    ok(InstallInf(SecondInf, &Error), "second SetupDiInstallClassA failed: %lu\n", Error);
    CheckProperty(4, DEVPROP_TYPE_UINT32, &Second4, sizeof(Second4));
    CheckProperty(5, DEVPROP_TYPE_STRING, FirstText, sizeof(FirstText));
    CheckProperty(6, DEVPROP_TYPE_STRING_LIST, SecondList, sizeof(SecondList));
    CheckProperty(8, DEVPROP_TYPE_UINT32, &Second8, sizeof(Second8));
    CheckMissing(9);
    ok(CountTestKeys() == 7, "CM_Get_Class_Property_Keys lists %lu test properties after the second install, expected 7\n",
       CountTestKeys());

Cleanup:
    ClassKey = SetupDiOpenClassRegKeyExW(NULL, KEY_ALL_ACCESS, DIOCR_INSTALLER, NULL, NULL);
    if (ClassKey != INVALID_HANDLE_VALUE)
    {
        RegDeleteTreeA(ClassKey, CLASS_GUID_TEXT);
        RegDeleteKeyA(ClassKey, CLASS_GUID_TEXT);
        RegCloseKey(ClassKey);
    }
}
