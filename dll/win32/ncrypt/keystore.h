/*
 * PROJECT:     LiberNT Cryptography
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Persisted key files of the software key storage provider
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef NCRYPT_KEYSTORE_H
#define NCRYPT_KEYSTORE_H

struct keystore_record
{
    WCHAR *algorithm;
    DWORD length;
    DWORD export_policy;
    DWORD key_usage;
    BYTE *blob;
    DWORD blob_size;
};

BOOL keystore_unique_name(const WCHAR *name, WCHAR *unique, DWORD count);
SECURITY_STATUS keystore_exists(const WCHAR *name, BOOL machine);
SECURITY_STATUS keystore_write(const WCHAR *name, BOOL machine, BOOL overwrite, const struct keystore_record *record);
SECURITY_STATUS keystore_read(const WCHAR *name, BOOL machine, struct keystore_record *record);
void keystore_free_record(struct keystore_record *record);
SECURITY_STATUS keystore_delete(const WCHAR *name, BOOL machine);
SECURITY_STATUS keystore_enum(BOOL machine, void **state, NCryptKeyName **key_name);

#endif
