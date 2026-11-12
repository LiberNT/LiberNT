/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Creating a key through a symbolic link whose target has no parent fails instead of recursing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <ndk/cmfuncs.h>
#include <ndk/obfuncs.h>

#define TEST_KEY L"SOFTWARE\\LiberNTTest\\RegCreateKeyLink"

static const WCHAR LinkTarget[] = L"\\Registry\\Machine\\SOFTWARE\\LiberNTTest\\RegCreateKeyLink\\missing\\target";

static
VOID
DeleteLink(
    _In_ HKEY Root)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"link");
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Link;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_OPENLINK, Root, NULL);
    Status = NtOpenKey(&Link, DELETE, &Attributes);
    ok(Status == STATUS_SUCCESS, "Opening the link itself failed: 0x%lx\n", Status);
    if (!NT_SUCCESS(Status)) return;

    Status = NtDeleteKey(Link);
    ok(Status == STATUS_SUCCESS, "Deleting the link failed: 0x%lx\n", Status);
    NtClose(Link);
}

START_TEST(RegCreateKeyLink)
{
    HKEY Root, Link, Key;
    LONG Error;

    Error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, TEST_KEY, 0, NULL, REG_OPTION_VOLATILE,
                            KEY_ALL_ACCESS, NULL, &Root, NULL);
    if (Error == ERROR_ACCESS_DENIED)
    {
        skip("Creating keys under HKLM needs administrator rights\n");
        return;
    }
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS) return;

    Error = RegCreateKeyExW(Root, L"link", 0, NULL, REG_OPTION_VOLATILE | REG_OPTION_CREATE_LINK,
                            KEY_ALL_ACCESS, NULL, &Link, NULL);
    ok_long(Error, ERROR_SUCCESS);
    if (Error != ERROR_SUCCESS)
    {
        RegCloseKey(Root);
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, TEST_KEY);
        return;
    }

    Error = RegSetValueExW(Link, L"SymbolicLinkValue", 0, REG_LINK, (const BYTE *)LinkTarget,
                           sizeof(LinkTarget) - sizeof(WCHAR));
    ok_long(Error, ERROR_SUCCESS);
    RegCloseKey(Link);

    Key = NULL;
    Error = RegOpenKeyExW(Root, L"link", 0, KEY_READ, &Key);
    ok_long(Error, ERROR_FILE_NOT_FOUND);
    if (Key != NULL) RegCloseKey(Key);

    Key = NULL;
    Error = RegCreateKeyExW(Root, L"link", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &Key, NULL);
    ok_long(Error, ERROR_FILE_NOT_FOUND);
    if (Key != NULL) RegCloseKey(Key);

    Key = NULL;
    Error = RegCreateKeyExW(Root, L"link\\below", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &Key, NULL);
    ok_long(Error, ERROR_FILE_NOT_FOUND);
    if (Key != NULL) RegCloseKey(Key);

    DeleteLink(Root);
    RegCloseKey(Root);
    Error = RegDeleteKeyW(HKEY_LOCAL_MACHINE, TEST_KEY);
    ok_long(Error, ERROR_SUCCESS);
}
