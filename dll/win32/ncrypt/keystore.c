/*
 * PROJECT:     LiberNT Cryptography
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Persisted key files of the software key storage provider
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wincrypt.h"
#include "ncrypt.h"
#include "bcrypt.h"
#include "ncrypt_internal.h"
#include "keystore.h"

#define KEYSTORE_MAGIC 0x4B544E4C
#define KEYSTORE_VERSION 1
#define KEYSTORE_UNIQUE_LENGTH 65

struct keystore_enum_state
{
    DWORD magic;
    DWORD index;
};

struct keystore_header
{
    DWORD magic;
    DWORD version;
    DWORD name_size;
    DWORD algorithm_size;
    DWORD length;
    DWORD export_policy;
    DWORD key_usage;
    DWORD protected_size;
};

static BOOL keystore_directory(BOOL machine, WCHAR *path, DWORD count, BOOL create)
{
    static const WCHAR suffix[] = L"\\Microsoft\\Crypto\\Keys";
    DWORD length;
    WCHAR *p;

    length = GetEnvironmentVariableW(machine ? L"ALLUSERSPROFILE" : L"APPDATA", path, count);
    if (!length || length >= count)
    {
        if (machine)
            return FALSE;
        length = GetEnvironmentVariableW(L"USERPROFILE", path, count);
        if (!length || length + 17 >= count)
            return FALSE;
        wcscat(path, L"\\AppData\\Roaming");
        length = wcslen(path);
    }
    if (length + ARRAYSIZE(suffix) >= count)
        return FALSE;
    wcscat(path, suffix);
    if (!create)
        return TRUE;
    for (p = path + length + 1; *p; p++)
    {
        if (*p != L'\\')
            continue;
        *p = 0;
        CreateDirectoryW(path, NULL);
        *p = L'\\';
    }
    return CreateDirectoryW(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

BOOL keystore_unique_name(const WCHAR *name, WCHAR *unique, DWORD count)
{
    BYTE digest[32];
    WCHAR *lower;
    size_t size;
    DWORD i;

    if (count < KEYSTORE_UNIQUE_LENGTH || !(lower = wcsdup(name)))
        return FALSE;
    for (i = 0; lower[i]; i++)
        lower[i] = towlower(lower[i]);
    size = wcslen(lower) * sizeof(WCHAR);
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (UCHAR *)lower, (ULONG)size, digest, sizeof(digest)))
    {
        free(lower);
        return FALSE;
    }
    free(lower);
    for (i = 0; i < sizeof(digest); i++)
        swprintf(unique + i * 2, 3, L"%02x", digest[i]);
    return TRUE;
}

static BOOL keystore_path(const WCHAR *name, BOOL machine, BOOL create, WCHAR *path, DWORD count)
{
    WCHAR unique[KEYSTORE_UNIQUE_LENGTH];
    size_t length;

    if (!keystore_unique_name(name, unique, ARRAYSIZE(unique)) || !keystore_directory(machine, path, count, create))
        return FALSE;
    length = wcslen(path);
    if (length + 1 + wcslen(unique) >= count)
        return FALSE;
    path[length] = L'\\';
    wcscpy(path + length + 1, unique);
    return TRUE;
}

static BYTE *read_file(const WCHAR *path, DWORD *size)
{
    HANDLE file;
    BYTE *data = NULL;
    DWORD read;

    *size = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return NULL;
    *size = GetFileSize(file, NULL);
    if (*size != INVALID_FILE_SIZE && *size >= sizeof(struct keystore_header) && (data = malloc(*size)) &&
        (!ReadFile(file, data, *size, &read, NULL) || read != *size))
    {
        free(data);
        data = NULL;
    }
    CloseHandle(file);
    return data;
}

static const struct keystore_header *parse_header(const BYTE *data, DWORD size, const WCHAR **name,
                                                  const WCHAR **algorithm, const BYTE **protected)
{
    const struct keystore_header *header = (const struct keystore_header *)data;
    DWORD needed;

    if (!data || size < sizeof(*header) || header->magic != KEYSTORE_MAGIC || header->version != KEYSTORE_VERSION)
        return NULL;
    needed = sizeof(*header) + header->name_size + header->algorithm_size + header->protected_size;
    if (needed > size || header->name_size < sizeof(WCHAR) || header->algorithm_size < sizeof(WCHAR) ||
        header->name_size % sizeof(WCHAR) || header->algorithm_size % sizeof(WCHAR))
        return NULL;
    *name = (const WCHAR *)(data + sizeof(*header));
    *algorithm = (const WCHAR *)(data + sizeof(*header) + header->name_size);
    *protected = data + sizeof(*header) + header->name_size + header->algorithm_size;
    if ((*name)[header->name_size / sizeof(WCHAR) - 1] || (*algorithm)[header->algorithm_size / sizeof(WCHAR) - 1])
        return NULL;
    return header;
}

SECURITY_STATUS keystore_exists(const WCHAR *name, BOOL machine)
{
    WCHAR path[MAX_PATH];

    if (!keystore_path(name, machine, FALSE, path, ARRAYSIZE(path)))
        return NTE_BAD_KEYSET;
    return GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES ? NTE_BAD_KEYSET : ERROR_SUCCESS;
}

SECURITY_STATUS keystore_write(const WCHAR *name, BOOL machine, BOOL overwrite, const struct keystore_record *record)
{
    struct keystore_header header;
    DATA_BLOB plain, protected = {0};
    WCHAR path[MAX_PATH];
    SECURITY_STATUS status = ERROR_SUCCESS;
    DWORD written, size;
    HANDLE file;
    BYTE *data;

    if (!keystore_path(name, machine, TRUE, path, ARRAYSIZE(path)))
        return NTE_INTERNAL_ERROR;
    plain.pbData = record->blob;
    plain.cbData = record->blob_size;
    if (!CryptProtectData(&plain, NULL, NULL, NULL, NULL,
                          CRYPTPROTECT_UI_FORBIDDEN | (machine ? CRYPTPROTECT_LOCAL_MACHINE : 0), &protected))
        return NTE_INTERNAL_ERROR;
    header.magic = KEYSTORE_MAGIC;
    header.version = KEYSTORE_VERSION;
    header.name_size = (DWORD)((wcslen(name) + 1) * sizeof(WCHAR));
    header.algorithm_size = (DWORD)((wcslen(record->algorithm) + 1) * sizeof(WCHAR));
    header.length = record->length;
    header.export_policy = record->export_policy;
    header.key_usage = record->key_usage;
    header.protected_size = protected.cbData;
    size = sizeof(header) + header.name_size + header.algorithm_size + header.protected_size;
    if (!(data = malloc(size)))
    {
        LocalFree(protected.pbData);
        return NTE_NO_MEMORY;
    }
    memcpy(data, &header, sizeof(header));
    memcpy(data + sizeof(header), name, header.name_size);
    memcpy(data + sizeof(header) + header.name_size, record->algorithm, header.algorithm_size);
    memcpy(data + sizeof(header) + header.name_size + header.algorithm_size, protected.pbData, protected.cbData);
    SecureZeroMemory(protected.pbData, protected.cbData);
    LocalFree(protected.pbData);

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, overwrite ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_SYSTEM, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        status = GetLastError() == ERROR_FILE_EXISTS ? NTE_EXISTS : NTE_INTERNAL_ERROR;
    }
    else
    {
        if (!WriteFile(file, data, size, &written, NULL) || written != size)
            status = NTE_INTERNAL_ERROR;
        CloseHandle(file);
        if (status)
            DeleteFileW(path);
    }
    free(data);
    return status;
}

SECURITY_STATUS keystore_read(const WCHAR *name, BOOL machine, struct keystore_record *record)
{
    const struct keystore_header *header;
    const WCHAR *stored_name, *algorithm;
    const BYTE *protected_data;
    DATA_BLOB protected, plain = {0};
    WCHAR path[MAX_PATH];
    SECURITY_STATUS status = NTE_BAD_KEYSET;
    BYTE *data;
    DWORD size;

    memset(record, 0, sizeof(*record));
    if (!keystore_path(name, machine, FALSE, path, ARRAYSIZE(path)) || !(data = read_file(path, &size)))
        return NTE_BAD_KEYSET;
    if ((header = parse_header(data, size, &stored_name, &algorithm, &protected_data)) && !lstrcmpiW(stored_name, name))
    {
        protected.pbData = (BYTE *)protected_data;
        protected.cbData = header->protected_size;
        status = NTE_BAD_DATA;
        if (CryptUnprotectData(&protected, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &plain))
        {
            if ((record->algorithm = wcsdup(algorithm)) && (record->blob = malloc(plain.cbData)))
            {
                memcpy(record->blob, plain.pbData, plain.cbData);
                record->blob_size = plain.cbData;
                record->length = header->length;
                record->export_policy = header->export_policy;
                record->key_usage = header->key_usage;
                status = ERROR_SUCCESS;
            }
            else
            {
                status = NTE_NO_MEMORY;
            }
            SecureZeroMemory(plain.pbData, plain.cbData);
            LocalFree(plain.pbData);
        }
    }
    free(data);
    if (status)
        keystore_free_record(record);
    return status;
}

void keystore_free_record(struct keystore_record *record)
{
    if (record->blob)
        SecureZeroMemory(record->blob, record->blob_size);
    free(record->blob);
    free(record->algorithm);
    memset(record, 0, sizeof(*record));
}

SECURITY_STATUS keystore_delete(const WCHAR *name, BOOL machine)
{
    WCHAR path[MAX_PATH];

    if (!keystore_path(name, machine, FALSE, path, ARRAYSIZE(path)))
        return NTE_BAD_KEYSET;
    return DeleteFileW(path) ? ERROR_SUCCESS : NTE_BAD_KEYSET;
}

SECURITY_STATUS keystore_enum(BOOL machine, void **state, NCryptKeyName **key_name)
{
    struct keystore_enum_state *enum_state = *state;
    const struct keystore_header *header;
    const WCHAR *name, *algorithm;
    const BYTE *protected_data;
    WCHAR directory[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW find;
    NCryptKeyName *result;
    DWORD skip, size;
    HANDLE handle;
    BYTE *data;

    if (!key_name || !state)
        return NTE_INVALID_PARAMETER;
    *key_name = NULL;
    if (enum_state && enum_state->magic != KEYSTORE_MAGIC)
        return NTE_INVALID_PARAMETER;
    if (!enum_state)
    {
        if (!(enum_state = calloc(1, sizeof(*enum_state))))
            return NTE_NO_MEMORY;
        enum_state->magic = KEYSTORE_MAGIC;
        *state = enum_state;
    }
    if (!keystore_directory(machine, directory, ARRAYSIZE(directory), FALSE) ||
        wcslen(directory) + 3 >= ARRAYSIZE(path))
        return NTE_NO_MORE_ITEMS;
    swprintf(path, ARRAYSIZE(path), L"%ls\\*", directory);
    if ((handle = FindFirstFileW(path, &find)) == INVALID_HANDLE_VALUE)
        return NTE_NO_MORE_ITEMS;
    skip = enum_state->index;
    do
    {
        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ||
            wcslen(directory) + 1 + wcslen(find.cFileName) >= ARRAYSIZE(path))
            continue;
        swprintf(path, ARRAYSIZE(path), L"%ls\\%ls", directory, find.cFileName);
        if (!(data = read_file(path, &size)))
            continue;
        if ((header = parse_header(data, size, &name, &algorithm, &protected_data)))
        {
            if (skip)
            {
                skip--;
            }
            else if ((result = malloc(sizeof(*result) + header->name_size + header->algorithm_size)))
            {
                result->pszName = (WCHAR *)(result + 1);
                result->pszAlgid = (WCHAR *)((BYTE *)result->pszName + header->name_size);
                memcpy(result->pszName, name, header->name_size);
                memcpy(result->pszAlgid, algorithm, header->algorithm_size);
                result->dwLegacyKeySpec = 0;
                result->dwFlags = machine ? NCRYPT_MACHINE_KEY_FLAG : 0;
                free(data);
                FindClose(handle);
                enum_state->index++;
                *key_name = result;
                return ERROR_SUCCESS;
            }
            else
            {
                free(data);
                FindClose(handle);
                return NTE_NO_MEMORY;
            }
        }
        free(data);
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
    return NTE_NO_MORE_ITEMS;
}
