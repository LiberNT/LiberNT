/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for SHSetLocalizedName, SHGetLocalizedName and SHRemoveLocalizedName
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <strsafe.h>

#define TEST_MODULE L"%SystemRoot%\\system32\\shell32.dll"
#define TEST_ID     21798

static const char folder_ini[] =
    "[.ShellClassInfo]\r\nLocalizedResourceName=@%SystemRoot%\\system32\\shell32.dll,-21798\r\n";
static const char file_ini[] =
    "[LocalizedFileNames]\r\nitem.txt=@%SystemRoot%\\system32\\shell32.dll,-21798\r\n";

static DWORD read_file(PCWSTR path, char *buffer, DWORD size)
{
    HANDLE file;
    DWORD read = 0;

    buffer[0] = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    ReadFile(file, buffer, size - 1, &read, NULL);
    CloseHandle(file);
    buffer[read] = 0;
    return read;
}

static void write_file(PCWSTR path, const char *data)
{
    HANDLE file;
    DWORD written;

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    WriteFile(file, data, lstrlenA(data), &written, NULL);
    CloseHandle(file);
}

static void check_ini(PCWSTR ini, const char *expected, PCSTR what)
{
    char data[1024];
    DWORD size;

    size = read_file(ini, data, sizeof(data));
    if (!expected)
    {
        ok(GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES, "%s: desktop.ini exists (%lu bytes)\n", what, size);
        return;
    }
    ok(size == (DWORD)lstrlenA(expected) && !memcmp(data, expected, size), "%s: desktop.ini '%s'\n", what, data);
    ok(GetFileAttributesW(ini) == (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM), "%s: desktop.ini attrs %lx\n",
       what, GetFileAttributesW(ini));
}

static void remove_tree(PCWSTR dir)
{
    WCHAR path[MAX_PATH];

    StringCchPrintfW(path, ARRAYSIZE(path), L"%ls\\desktop.ini", dir);
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(path);
    StringCchPrintfW(path, ARRAYSIZE(path), L"%ls\\item.txt", dir);
    DeleteFileW(path);
    SetFileAttributesW(dir, FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(dir);
}

static void test_folder(PCWSTR dir)
{
    WCHAR ini[MAX_PATH], module[MAX_PATH];
    int id = 0xdead;
    HRESULT hr;

    StringCchPrintfW(ini, ARRAYSIZE(ini), L"%ls\\desktop.ini", dir);

    hr = SHGetLocalizedName(dir, module, ARRAYSIZE(module), &id);
    ok(hr == E_INVALIDARG, "unset Get hr 0x%lx\n", hr);

    hr = SHRemoveLocalizedName(dir);
    ok(hr == S_OK, "Remove on unset folder hr 0x%lx\n", hr);
    ok(GetFileAttributesW(dir) == FILE_ATTRIBUTE_DIRECTORY, "attrs after unset Remove %lx\n", GetFileAttributesW(dir));

    hr = SHSetLocalizedName(dir, TEST_MODULE, TEST_ID);
    ok(hr == S_OK, "Set hr 0x%lx\n", hr);
    ok(GetFileAttributesW(dir) == (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY), "attrs after Set %lx\n",
       GetFileAttributesW(dir));
    check_ini(ini, folder_ini, "folder Set");

    id = 0xdead;
    module[0] = 0;
    hr = SHGetLocalizedName(dir, module, ARRAYSIZE(module), &id);
    ok(hr == S_OK, "Get hr 0x%lx\n", hr);
    ok(id == TEST_ID, "Get id %d\n", id);
    ok(!wcscmp(module, TEST_MODULE), "Get module '%ls'\n", module);

    hr = SHGetLocalizedName(dir, module, 3, &id);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER), "Get small buffer hr 0x%lx\n", hr);

    SetFileAttributesW(dir, FILE_ATTRIBUTE_NORMAL);
    hr = SHGetLocalizedName(dir, module, ARRAYSIZE(module), &id);
    ok(hr == E_INVALIDARG, "Get without READONLY hr 0x%lx\n", hr);
    SetFileAttributesW(dir, FILE_ATTRIBUTE_READONLY);

    hr = SHRemoveLocalizedName(dir);
    ok(hr == S_OK, "Remove hr 0x%lx\n", hr);
    ok(GetFileAttributesW(dir) == (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY), "attrs after Remove %lx\n",
       GetFileAttributesW(dir));
    check_ini(ini, NULL, "folder Remove");

    id = 0xdead;
    hr = SHGetLocalizedName(dir, module, ARRAYSIZE(module), &id);
    ok(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "Get after Remove hr 0x%lx\n", hr);

    hr = SHRemoveLocalizedName(dir);
    ok(hr == S_OK, "second Remove hr 0x%lx\n", hr);

    write_file(ini, "[.ShellClassInfo]\r\nIconResource=shell32.dll,4\r\n");
    hr = SHSetLocalizedName(dir, TEST_MODULE, TEST_ID);
    ok(hr == S_OK, "Set with other keys hr 0x%lx\n", hr);
    {
        char data[1024];

        read_file(ini, data, sizeof(data));
        ok(strstr(data, "IconResource=shell32.dll,4") != NULL, "other key lost after Set\n");
        ok(strstr(data, "LocalizedResourceName=@%SystemRoot%\\system32\\shell32.dll,-21798") != NULL, "Set with other keys missing value\n");
        ok(GetFileAttributesW(ini) == (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM), "ini attrs after Set %lx\n", GetFileAttributesW(ini));
    }
    hr = SHRemoveLocalizedName(dir);
    ok(hr == S_OK, "Remove with other keys hr 0x%lx\n", hr);
    {
        char data[1024];
        DWORD size;

        size = read_file(ini, data, sizeof(data));
        ok(size == 47 && !memcmp(data, "[.ShellClassInfo]\r\nIconResource=shell32.dll,4\r\n", 47),
           "ini after Remove: %lu bytes\n", size);
        ok(GetFileAttributesW(ini) == (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM), "ini attrs after Remove %lx\n", GetFileAttributesW(ini));
    }
    SetFileAttributesW(ini, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(ini);
}

static void test_file(PCWSTR dir)
{
    WCHAR file[MAX_PATH], ini[MAX_PATH], module[MAX_PATH];
    int id = 0xdead;
    HRESULT hr;

    StringCchPrintfW(file, ARRAYSIZE(file), L"%ls\\item.txt", dir);
    StringCchPrintfW(ini, ARRAYSIZE(ini), L"%ls\\desktop.ini", dir);
    write_file(file, "x");

    hr = SHGetLocalizedName(file, module, ARRAYSIZE(module), &id);
    ok(hr == E_INVALIDARG, "file unset Get hr 0x%lx\n", hr);

    hr = SHSetLocalizedName(file, TEST_MODULE, TEST_ID);
    ok(hr == S_OK, "file Set hr 0x%lx\n", hr);
    check_ini(ini, file_ini, "file Set");
    ok(GetFileAttributesW(dir) == (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY), "folder attrs after file Set %lx\n",
       GetFileAttributesW(dir));

    module[0] = 0;
    id = 0xdead;
    hr = SHGetLocalizedName(file, module, ARRAYSIZE(module), &id);
    ok(hr == S_OK, "file Get hr 0x%lx\n", hr);
    ok(id == TEST_ID, "file Get id %d\n", id);
    ok(!wcscmp(module, TEST_MODULE), "file Get module '%ls'\n", module);

    hr = SHRemoveLocalizedName(file);
    ok(hr == S_OK, "file Remove hr 0x%lx\n", hr);
    check_ini(ini, NULL, "file Remove");

    hr = SHGetLocalizedName(file, module, ARRAYSIZE(module), &id);
    ok(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "file Get after Remove hr 0x%lx\n", hr);
}

START_TEST(LocalizedName)
{
    WCHAR dir[MAX_PATH], dir2[MAX_PATH];
    HRESULT hr;

    CoInitialize(NULL);

    GetTempPathW(MAX_PATH, dir);
    StringCchCopyW(dir2, MAX_PATH, dir);
    StringCchCatW(dir, MAX_PATH, L"libernt_locname");
    StringCchCatW(dir2, MAX_PATH, L"libernt_locname2");
    remove_tree(dir);
    remove_tree(dir2);
    ok(CreateDirectoryW(dir, NULL), "CreateDirectoryW failed: %lu\n", GetLastError());
    ok(CreateDirectoryW(dir2, NULL), "CreateDirectoryW failed: %lu\n", GetLastError());

    test_folder(dir);
    test_file(dir2);

    hr = SHRemoveLocalizedName(L"C:\\libernt-no-such-dir\\x");
    ok(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "Remove missing path hr 0x%lx\n", hr);
    hr = SHSetLocalizedName(L"C:\\libernt-no-such-dir\\x", TEST_MODULE, TEST_ID);
    ok(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "Set missing path hr 0x%lx\n", hr);

    remove_tree(dir);
    remove_tree(dir2);
    ok(GetFileAttributesW(dir) == INVALID_FILE_ATTRIBUTES, "folder not removed\n");

    CoUninitialize();
}
