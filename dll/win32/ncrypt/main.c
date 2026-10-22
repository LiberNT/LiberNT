/*
 * New cryptographic library (ncrypt.dll)
 *
 * Copyright 2016 Alex Henrie
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdlib.h>

#include "ntstatus.h"
#ifdef __REACTOS__
#define WIN32_NO_STATUS
#endif
#include "windef.h"
#include "winbase.h"
#include "ncrypt.h"
#include "bcrypt.h"
#include "ncrypt_internal.h"
#ifdef __REACTOS__
#include "keystore.h"
#endif
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ncrypt);

static SECURITY_STATUS map_ntstatus(NTSTATUS status)
{
    switch (status)
    {
    case STATUS_INVALID_HANDLE:    return NTE_INVALID_HANDLE;
    case STATUS_INVALID_SIGNATURE: return NTE_BAD_SIGNATURE;
    case STATUS_SUCCESS:           return ERROR_SUCCESS;
    case STATUS_INVALID_PARAMETER: return NTE_INVALID_PARAMETER;
    case STATUS_NO_MEMORY:         return NTE_NO_MEMORY;
    case STATUS_NOT_SUPPORTED:     return NTE_NOT_SUPPORTED;
    case NTE_BAD_DATA:             return NTE_BAD_DATA;
    case STATUS_BUFFER_TOO_SMALL:  return NTE_BUFFER_TOO_SMALL;
    default:
        FIXME("unhandled status %#lx\n", status);
        return NTE_INTERNAL_ERROR;
    }
}

static struct object *allocate_object(enum object_type type)
{
    struct object *ret;
    if (!(ret = calloc(1, sizeof(*ret)))) return NULL;
    ret->type = type;
    ret->refs = 1;
    return ret;
}

static void provider_addref(NCRYPT_PROV_HANDLE handle)
{
    struct object *object = (struct object *)handle;
    InterlockedIncrement(&object->refs);
}

static struct object_property *get_object_property(struct object *object, const WCHAR *name)
{
    unsigned int i;
    for (i = 0; i < object->num_properties; i++)
    {
        struct object_property *property = &object->properties[i];
        if (!lstrcmpW(property->key, name)) return property;
    }
    return NULL;
}

struct object_property *add_object_property(struct object *object, const WCHAR *name)
{
    struct object_property *property;

    if (!object->num_properties)
    {
        if (!(object->properties = malloc(sizeof(*property))))
        {
            ERR("Error allocating memory.\n");
            return NULL;
        }
        property = &object->properties[object->num_properties++];
    }
    else
    {
        struct object_property *tmp;
        if (!(tmp = realloc(object->properties, sizeof(*property) * (object->num_properties + 1))))
        {
            ERR("Error allocating memory.\n");
            return NULL;
        }
        object->properties = tmp;
        property = &object->properties[object->num_properties++];
    }

    memset(property, 0, sizeof(*property));
    if (!(property->key = wcsdup(name)))
    {
        ERR("Error allocating memory.\n");
        return NULL;
    }

    return property;
}

static SECURITY_STATUS set_object_property(struct object *object, const WCHAR *name, BYTE *value, DWORD value_size)
{
    struct object_property *property = get_object_property(object, name);
    void *tmp;

    if (!property && !(property = add_object_property(object, name))) return NTE_NO_MEMORY;

    property->value_size = value_size;
    if (!(tmp = realloc(property->value, value_size)))
    {
        ERR("Error allocating memory.\n");
        free(property->key);
        property->key = NULL;
        return NTE_NO_MEMORY;
    }

    property->value = tmp;
    memcpy(property->value, value, value_size);

    return ERROR_SUCCESS;
}

static struct object *create_key_object(enum algid algid, NCRYPT_PROV_HANDLE provider)
{
    NCRYPT_SUPPORTED_LENGTHS supported_lengths = {512, 16384, 8, 1024};
    struct object *object;
    DWORD dw_value;

    switch (algid)
    {
    case RSA:
        if (!(object = allocate_object(KEY))) return NULL;

        object->key.algid = RSA;
        set_object_property(object, NCRYPT_ALGORITHM_PROPERTY, (BYTE *)BCRYPT_RSA_ALGORITHM,
                            sizeof(BCRYPT_RSA_ALGORITHM));
        set_object_property(object, NCRYPT_ALGORITHM_GROUP_PROPERTY, (BYTE *)BCRYPT_RSA_ALGORITHM,
                            sizeof(BCRYPT_RSA_ALGORITHM));
        set_object_property(object, NCRYPT_LENGTHS_PROPERTY, (BYTE *)&supported_lengths,
                            sizeof(supported_lengths));
        dw_value = 128;
        set_object_property(object, NCRYPT_BLOCK_LENGTH_PROPERTY, (BYTE *)&dw_value, sizeof(dw_value));
        dw_value = 128;
        set_object_property(object, BCRYPT_SIGNATURE_LENGTH, (BYTE *)&dw_value, sizeof(dw_value));
        break;

    case ECDSA_P256:
        if (!(object = allocate_object(KEY))) return NULL;

        object->key.algid = ECDSA_P256;
        break;

    default:
        ERR("Invalid algid %#x\n", algid);
        return NULL;
    }

    dw_value = 0;
    set_object_property(object, NCRYPT_EXPORT_POLICY_PROPERTY, (BYTE *)&dw_value, sizeof(dw_value));
    dw_value = NCRYPT_ALLOW_ALL_USAGES;
    set_object_property(object, NCRYPT_KEY_USAGE_PROPERTY, (BYTE *)&dw_value, sizeof(dw_value));
    dw_value = 0;
    set_object_property(object, NCRYPT_KEY_TYPE_PROPERTY, (BYTE *)&dw_value, sizeof(dw_value));
    provider_addref(provider);
    set_object_property(object, NCRYPT_PROVIDER_HANDLE_PROPERTY, (BYTE *)&provider, sizeof(provider));
    return object;
}

#ifdef __REACTOS__
static SECURITY_STATUS keystore_set_identity(struct object *object, const WCHAR *name, DWORD flags)
{
    WCHAR unique[128];
    DWORD type = flags & NCRYPT_MACHINE_KEY_FLAG;

    if (!(object->key.name = wcsdup(name))) return NTE_NO_MEMORY;
    object->key.flags = flags;
    set_object_property(object, NCRYPT_NAME_PROPERTY, (BYTE *)name, (lstrlenW(name) + 1) * sizeof(WCHAR));
    if (keystore_unique_name(name, unique, sizeof(unique) / sizeof(unique[0])))
        set_object_property(object, NCRYPT_UNIQUE_NAME_PROPERTY, (BYTE *)unique, (lstrlenW(unique) + 1) * sizeof(WCHAR));
    set_object_property(object, NCRYPT_KEY_TYPE_PROPERTY, (BYTE *)&type, sizeof(type));
    return ERROR_SUCCESS;
}

static DWORD dword_property(struct object *object, const WCHAR *name, DWORD fallback)
{
    struct object_property *property = get_object_property(object, name);

    return property && property->value_size == sizeof(DWORD) ? *(DWORD *)property->value : fallback;
}

static SECURITY_STATUS keystore_save(struct object *object)
{
    const WCHAR *blob_type = object->key.algid == RSA ? BCRYPT_RSAFULLPRIVATE_BLOB : BCRYPT_ECCPRIVATE_BLOB;
    struct keystore_record record = {0};
    SECURITY_STATUS ret;
    NTSTATUS status;
    ULONG size = 0;

    record.algorithm = object->key.algid == RSA ? (WCHAR *)NCRYPT_RSA_ALGORITHM : (WCHAR *)NCRYPT_ECDSA_P256_ALGORITHM;
    record.length = dword_property(object, NCRYPT_LENGTH_PROPERTY, 0);
    record.export_policy = dword_property(object, NCRYPT_EXPORT_POLICY_PROPERTY, 0);
    record.key_usage = dword_property(object, NCRYPT_KEY_USAGE_PROPERTY, NCRYPT_ALLOW_ALL_USAGES);
    if ((status = BCryptExportKey(object->key.bcrypt_key, NULL, blob_type, NULL, 0, &size, 0))) return map_ntstatus(status);
    if (!(record.blob = malloc(size))) return NTE_NO_MEMORY;
    if ((status = BCryptExportKey(object->key.bcrypt_key, NULL, blob_type, record.blob, size, &size, 0)))
    {
        free(record.blob);
        return map_ntstatus(status);
    }
    record.blob_size = size;
    ret = keystore_write(object->key.name, !!(object->key.flags & NCRYPT_MACHINE_KEY_FLAG),
                         !!(object->key.flags & NCRYPT_OVERWRITE_KEY_FLAG), &record);
    SecureZeroMemory(record.blob, record.blob_size);
    free(record.blob);
    return ret;
}

static BOOL is_private_blob(const WCHAR *type)
{
    static const WCHAR *const private_blobs[] =
    {
        BCRYPT_RSAPRIVATE_BLOB, BCRYPT_RSAFULLPRIVATE_BLOB, BCRYPT_ECCPRIVATE_BLOB, BCRYPT_DSA_PRIVATE_BLOB,
        BCRYPT_PRIVATE_KEY_BLOB, LEGACY_RSAPRIVATE_BLOB, LEGACY_DSA_V2_PRIVATE_BLOB,
    };
    unsigned int i;

    for (i = 0; type && i < sizeof(private_blobs) / sizeof(private_blobs[0]); i++)
        if (!lstrcmpW(type, private_blobs[i])) return TRUE;
    return FALSE;
}
#endif

SECURITY_STATUS WINAPI NCryptCreatePersistedKey(NCRYPT_PROV_HANDLE provider, NCRYPT_KEY_HANDLE *key,
                                                const WCHAR *algid, const WCHAR *name, DWORD keyspec, DWORD flags)
{
    struct object *object;

    TRACE("(%#Ix, %p, %s, %s, %#lx, %#lx)\n", provider, key, wine_dbgstr_w(algid),
          wine_dbgstr_w(name), keyspec, flags);

    if (!provider) return NTE_INVALID_HANDLE;
    if (!algid) return HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER);
#ifdef __REACTOS__
    if (name && !(flags & NCRYPT_OVERWRITE_KEY_FLAG) &&
        keystore_exists(name, !!(flags & NCRYPT_MACHINE_KEY_FLAG)) == ERROR_SUCCESS)
        return NTE_EXISTS;
#else
    if (name) FIXME("Persistent keys are not supported\n");
#endif

    if (!lstrcmpiW(algid, BCRYPT_RSA_ALGORITHM))
    {
        NTSTATUS status;
        DWORD default_bitlen = 1024;

        if (!(object = create_key_object(RSA, provider)))
        {
            ERR("Error allocating memory\n");
            return NTE_NO_MEMORY;
        }

        status = BCryptGenerateKeyPair(BCRYPT_RSA_ALG_HANDLE, &object->key.bcrypt_key, default_bitlen, 0);
        if (status != STATUS_SUCCESS)
        {
            ERR("Error generating key pair %#lx\n", status);
            free(object);
            return map_ntstatus(status);
        }

        set_object_property(object, NCRYPT_LENGTH_PROPERTY, (BYTE *)&default_bitlen, sizeof(default_bitlen));
        set_object_property(object, BCRYPT_PUBLIC_KEY_LENGTH, (BYTE *)&default_bitlen, sizeof(default_bitlen));
    }
    else if (!lstrcmpiW(algid, BCRYPT_ECDSA_P256_ALGORITHM))
    {
        NTSTATUS status;
        DWORD bitlen = 256;

        if (!(object = create_key_object(ECDSA_P256, provider)))
            return NTE_NO_MEMORY;

        status = BCryptGenerateKeyPair(BCRYPT_ECDSA_P256_ALG_HANDLE, &object->key.bcrypt_key, bitlen, 0);
        if (status != STATUS_SUCCESS)
        {
            free(object);
            return map_ntstatus(status);
        }

        set_object_property(object, NCRYPT_LENGTH_PROPERTY, (BYTE *)&bitlen, sizeof(bitlen));
        set_object_property(object, BCRYPT_PUBLIC_KEY_LENGTH, (BYTE *)&bitlen, sizeof(bitlen));
    }
    else
    {
        FIXME("Algorithm not handled %s\n", wine_dbgstr_w(algid));
        return NTE_NOT_SUPPORTED;
    }

#ifdef __REACTOS__
    if (name && keystore_set_identity(object, name, flags))
    {
        NCryptFreeObject((NCRYPT_HANDLE)object);
        return NTE_NO_MEMORY;
    }
#endif
    *key = (NCRYPT_KEY_HANDLE)object;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptDecrypt(NCRYPT_KEY_HANDLE key, BYTE *input, DWORD insize, void *padding,
                                     BYTE *output, DWORD outsize, DWORD *result, DWORD flags)
{
#ifdef __REACTOS__
    struct object *key_object = (struct object *)key;

    TRACE("(%#Ix, %p, %lu, %p, %p, %lu, %p, %#lx)\n", key, input, insize, padding,
          output, outsize, result, flags);

    if (flags & ~(NCRYPT_NO_PADDING_FLAG | NCRYPT_PAD_OAEP_FLAG | NCRYPT_PAD_PKCS1_FLAG | NCRYPT_SILENT_FLAG))
        return NTE_BAD_FLAGS;
    if (!key_object || key_object->type != KEY) return NTE_INVALID_HANDLE;
    if (!key_object->key.finalized) return NTE_BAD_KEY_STATE;
    return map_ntstatus(BCryptDecrypt(key_object->key.bcrypt_key, input, insize, padding, NULL, 0, output, outsize,
                                      result, flags & ~NCRYPT_SILENT_FLAG));
#else
    FIXME("(%#Ix, %p, %lu, %p, %p, %lu, %p, %#lx): stub\n", key, input, insize, padding,
          output, outsize, result, flags);
    return NTE_NOT_SUPPORTED;
#endif
}

SECURITY_STATUS WINAPI NCryptDeleteKey(NCRYPT_KEY_HANDLE key, DWORD flags)
{
#ifdef __REACTOS__
    struct object *object = (struct object *)key;
    SECURITY_STATUS ret = ERROR_SUCCESS;

    TRACE("(%#Ix, %#lx)\n", key, flags);

    if (!object || object->type != KEY) return NTE_INVALID_HANDLE;
    if (object->key.name)
        ret = keystore_delete(object->key.name, !!(object->key.flags & NCRYPT_MACHINE_KEY_FLAG));
    if (ret == ERROR_SUCCESS) NCryptFreeObject(key);
    return ret;
#else
    FIXME("(%#Ix, %#lx): stub\n", key, flags);
    return NTE_NOT_SUPPORTED;
#endif
}

SECURITY_STATUS WINAPI NCryptEncrypt(NCRYPT_KEY_HANDLE key, BYTE *input, DWORD insize, void *padding,
                                     BYTE *output, DWORD outsize, DWORD *result, DWORD flags)
{
    struct object *key_object = (struct object *)key;

    TRACE("(%#Ix, %p, %lu, %p, %p, %lu, %p, %#lx)\n", key, input, insize, padding,
          output, outsize, result, flags);

    if (flags & ~(NCRYPT_NO_PADDING_FLAG | NCRYPT_PAD_OAEP_FLAG
                | NCRYPT_PAD_PKCS1_FLAG | NCRYPT_SILENT_FLAG))
    {
        FIXME("Flags %lx not supported\n", flags);
        return NTE_BAD_FLAGS;
    }

#ifdef __REACTOS__
    if (flags & NCRYPT_PAD_OAEP_FLAG)
    {
        FIXME("oaep padding not supported\n");
        return NTE_NOT_SUPPORTED;
    }

    if (!key_object || key_object->type != KEY) return NTE_INVALID_HANDLE;
    if (!key_object->key.finalized) return NTE_BAD_KEY_STATE;
#else
    if (flags & NCRYPT_NO_PADDING_FLAG || flags & NCRYPT_PAD_OAEP_FLAG)
    {
        FIXME("No padding and oaep padding not supported\n");
        return NTE_NOT_SUPPORTED;
    }

    if (key_object->type != KEY) return NTE_INVALID_HANDLE;
#endif

    return map_ntstatus(BCryptEncrypt(key_object->key.bcrypt_key, input, insize, padding,
                                      NULL, 0, output, outsize, result, flags));
}

SECURITY_STATUS WINAPI NCryptEnumAlgorithms(NCRYPT_PROV_HANDLE provider, DWORD alg_ops,
                                            DWORD *alg_count, NCryptAlgorithmName **alg_list, DWORD flags)
{
#ifdef __REACTOS__
    static const struct
    {
        const WCHAR *name;
        DWORD class;
        DWORD operations;
    }
    algorithms[] =
    {
        { NCRYPT_RSA_ALGORITHM, BCRYPT_ASYMMETRIC_ENCRYPTION_INTERFACE,
          NCRYPT_ASYMMETRIC_ENCRYPTION_OPERATION | NCRYPT_SIGNATURE_OPERATION },
        { NCRYPT_ECDSA_P256_ALGORITHM, BCRYPT_SIGNATURE_INTERFACE, NCRYPT_SIGNATURE_OPERATION },
    };
    NCryptAlgorithmName *names;
    DWORD count = 0, size = 0, i;
    WCHAR *strings;

    TRACE("(%#Ix, %#lx, %p, %p, %#lx)\n", provider, alg_ops, alg_count, alg_list, flags);

    if (!provider) return NTE_INVALID_HANDLE;
    if (!alg_count || !alg_list) return NTE_INVALID_PARAMETER;
    if (flags & ~NCRYPT_SILENT_FLAG) return NTE_BAD_FLAGS;
    for (i = 0; i < sizeof(algorithms) / sizeof(algorithms[0]); i++)
    {
        if (alg_ops && !(algorithms[i].operations & alg_ops)) continue;
        size += (lstrlenW(algorithms[i].name) + 1) * sizeof(WCHAR);
        count++;
    }
    if (!(names = malloc(count * sizeof(*names) + size))) return NTE_NO_MEMORY;
    strings = (WCHAR *)(names + count);
    count = 0;
    for (i = 0; i < sizeof(algorithms) / sizeof(algorithms[0]); i++)
    {
        if (alg_ops && !(algorithms[i].operations & alg_ops)) continue;
        names[count].pszName = strings;
        lstrcpyW(strings, algorithms[i].name);
        strings += lstrlenW(algorithms[i].name) + 1;
        names[count].dwClass = algorithms[i].class;
        names[count].dwAlgOperations = algorithms[i].operations;
        names[count].dwFlags = 0;
        count++;
    }
    *alg_count = count;
    *alg_list = names;
    return ERROR_SUCCESS;
#else
    FIXME("(%#Ix, %#lx, %p, %p, %#lx): stub\n", provider, alg_ops, alg_count, alg_list, flags);
    return NTE_NOT_SUPPORTED;
#endif
}

SECURITY_STATUS WINAPI NCryptEnumKeys(NCRYPT_PROV_HANDLE provider, const WCHAR *scope,
                                      NCryptKeyName **key_name, PVOID *enum_state, DWORD flags)
{
#ifdef __REACTOS__
    TRACE("(%#Ix, %p, %p, %p, %#lx)\n", provider, scope, key_name, enum_state, flags);

    if (!provider) return NTE_INVALID_HANDLE;
    if (flags & ~(NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG)) return NTE_BAD_FLAGS;
    return keystore_enum(!!(flags & NCRYPT_MACHINE_KEY_FLAG), enum_state, key_name);
#else
    FIXME("(%#Ix, %p, %p, %p, %#lx): stub\n", provider, scope, key_name, enum_state, flags);
    return NTE_NOT_SUPPORTED;
#endif
}

SECURITY_STATUS WINAPI NCryptFinalizeKey(NCRYPT_KEY_HANDLE handle, DWORD flags)
{
    struct object *object = (struct object *)handle;
    DWORD key_length;
    struct object_property *prop;
    NTSTATUS status;
#ifdef __REACTOS__
    SECURITY_STATUS ret;
#endif

    TRACE("(%#Ix, %#lx)\n", handle, flags);

    if (!object || object->type != KEY) return NTE_INVALID_HANDLE;

    if (!(prop = get_object_property(object, NCRYPT_LENGTH_PROPERTY))) return NTE_INVALID_HANDLE;
    key_length = *(DWORD *)prop->value;
    status = BCryptSetProperty(object->key.bcrypt_key, BCRYPT_KEY_LENGTH, (UCHAR *)&key_length, sizeof(key_length), 0);
    if (status != STATUS_SUCCESS)
    {
        ERR("Error setting key length property\n");
        return map_ntstatus(status);
    }

    status = BCryptFinalizeKeyPair(object->key.bcrypt_key, 0);
    if (status != STATUS_SUCCESS)
    {
        ERR("Error finalizing key pair\n");
        return map_ntstatus(status);
    }

#ifdef __REACTOS__
    if (object->key.name && (ret = keystore_save(object))) return ret;
    object->key.finalized = TRUE;
#endif
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptFreeBuffer(PVOID buf)
{
#ifdef __REACTOS__
    TRACE("(%p)\n", buf);
    free(buf);
    return ERROR_SUCCESS;
#else
    FIXME("(%p): stub\n", buf);
    return NTE_NOT_SUPPORTED;
#endif
}

static SECURITY_STATUS free_key_object(struct key *key)
{
    return map_ntstatus( BCryptDestroyKey(key->bcrypt_key) );
}

SECURITY_STATUS WINAPI NCryptFreeObject(NCRYPT_HANDLE handle)
{
    struct object *object = (struct object *)handle;
    SECURITY_STATUS ret = STATUS_SUCCESS;
    unsigned int i;

    TRACE("(%#Ix)\n", handle);

    if (!object)
    {
        WARN("invalid handle %#Ix\n", handle);
        return NTE_INVALID_HANDLE;
    }

    switch (object->type)
    {
    case KEY:
    {
        if ((ret = free_key_object(&object->key))) return ret;
#ifdef __REACTOS__
        free(object->key.name);
#endif
        break;
    }
    case STORAGE_PROVIDER:
        if (InterlockedDecrement(&object->refs))
            return 0;
        break;

    default:
        WARN("invalid handle %#Ix\n", handle);
        return NTE_INVALID_HANDLE;
    }

    for (i = 0; i < object->num_properties; i++)
    {
        if (object->properties[i].key && !wcscmp(object->properties[i].key, NCRYPT_PROVIDER_HANDLE_PROPERTY))
            NCryptFreeObject(*(NCRYPT_HANDLE *)object->properties[i].value);

        free(object->properties[i].key);
        free(object->properties[i].value);
    }
    free(object->properties);
    free(object);
    return ret;
}

SECURITY_STATUS WINAPI NCryptGetProperty(NCRYPT_HANDLE handle, const WCHAR *name, BYTE *output,
                                         DWORD outsize, DWORD *result, DWORD flags)
{
    struct object *object = (struct object *)handle;
    const struct object_property *property;

    TRACE("(%#Ix, %s, %p, %lu, %p, %#lx)\n", handle, wine_dbgstr_w(name), output, outsize, result, flags);
    if (flags) FIXME("flags %#lx not supported\n", flags);

    if (!object) return NTE_INVALID_HANDLE;
#ifdef __REACTOS__
    if (!(property = get_object_property(object, name)))
    {
        static const WCHAR *const unset_properties[] =
        {
            NCRYPT_UI_POLICY_PROPERTY,
            NCRYPT_WINDOW_HANDLE_PROPERTY,
            NCRYPT_USE_CONTEXT_PROPERTY,
            NCRYPT_LAST_MODIFIED_PROPERTY,
            NCRYPT_CERTIFICATE_PROPERTY,
        };
        unsigned int i;

        if (object->type != KEY || !name) return NTE_INVALID_PARAMETER;
        if (!lstrcmpW(name, NCRYPT_SECURITY_DESCR_PROPERTY) &&
            !(flags & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                       DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION)))
        {
            return NTE_BAD_FLAGS;
        }
        for (i = 0; i < sizeof(unset_properties) / sizeof(unset_properties[0]); i++)
        {
            if (!lstrcmpW(name, unset_properties[i])) return NTE_NOT_FOUND;
        }
        return NTE_NOT_SUPPORTED;
    }
#else
    if (!(property = get_object_property(object, name))) return NTE_INVALID_PARAMETER;
#endif

    *result = property->value_size;
    if (!output) return ERROR_SUCCESS;
    if (outsize < property->value_size) return NTE_BUFFER_TOO_SMALL;

    memcpy(output, property->value, property->value_size);
    if (!wcscmp(name, NCRYPT_PROVIDER_HANDLE_PROPERTY))
        provider_addref(*(NCRYPT_PROV_HANDLE*)property->value);
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptImportKey(NCRYPT_PROV_HANDLE provider, NCRYPT_KEY_HANDLE decrypt_key,
                                       const WCHAR *type, NCryptBufferDesc *params, NCRYPT_KEY_HANDLE *handle,
                                       BYTE *data, DWORD datasize, DWORD flags)
{
    BCRYPT_KEY_BLOB *header = (BCRYPT_KEY_BLOB *)data;
    struct object *object;

    TRACE("(%#Ix, %#Ix, %s, %p, %p, %p, %lu, %#lx)\n", provider, decrypt_key, wine_dbgstr_w(type),
          params, handle, data, datasize, flags);

    if (decrypt_key)
    {
        FIXME("Key blob decryption not implemented\n");
        return NTE_NOT_SUPPORTED;
    }
    if (params)
    {
        FIXME("Parameter information not implemented\n");
        return NTE_NOT_SUPPORTED;
    }
    if (flags == NCRYPT_SILENT_FLAG)
    {
        FIXME("Silent flag not implemented\n");
    }
    else if (flags)
    {
        WARN("Invalid flags %#lx\n", flags);
        return NTE_BAD_FLAGS;
    }
    if (!header) return NTE_INVALID_PARAMETER;

    switch(header->Magic)
    {
    case BCRYPT_RSAFULLPRIVATE_MAGIC:
    case BCRYPT_RSAPRIVATE_MAGIC:
    case BCRYPT_RSAPUBLIC_MAGIC:
    {
        NTSTATUS status;
        BCRYPT_RSAKEY_BLOB *rsablob = (BCRYPT_RSAKEY_BLOB *)data;

        if (!(object = create_key_object(RSA, provider)))
        {
            ERR("Error allocating memory\n");
            return NTE_NO_MEMORY;
        }

        status = BCryptImportKeyPair(BCRYPT_RSA_ALG_HANDLE, NULL, type, &object->key.bcrypt_key, data, datasize, 0);
        if (status != STATUS_SUCCESS)
        {
            WARN("Error importing key pair %#lx\n", status);
            free(object);
            return map_ntstatus(status);
        }

        set_object_property(object, NCRYPT_LENGTH_PROPERTY, (BYTE *)&rsablob->BitLength, sizeof(rsablob->BitLength));
        set_object_property(object, BCRYPT_PUBLIC_KEY_LENGTH, (BYTE *)&rsablob->BitLength, sizeof(rsablob->BitLength));
#ifdef __REACTOS__
        object->key.finalized = TRUE;
#endif
        break;
    }
    default:
        FIXME("Unhandled key magic %#lx\n", header->Magic);
        return NTE_INVALID_PARAMETER;
    }

    *handle = (NCRYPT_KEY_HANDLE)object;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptExportKey(NCRYPT_KEY_HANDLE key, NCRYPT_KEY_HANDLE encrypt_key, const WCHAR *type,
                                       NCryptBufferDesc *params, BYTE *output, DWORD output_len, DWORD *ret_len,
                                       DWORD flags)
{
    struct object *object = (struct object *)key;

    TRACE("(%#Ix, %#Ix, %s, %p, %p, %lu, %p, %#lx)\n", key, encrypt_key, wine_dbgstr_w(type), params, output,
          output_len, ret_len, flags);

    if (encrypt_key)
    {
        FIXME("Key blob encryption not implemented\n");
        return NTE_NOT_SUPPORTED;
    }
    if (params)
    {
        FIXME("Parameter information not implemented\n");
        return NTE_NOT_SUPPORTED;
    }
    if (flags == NCRYPT_SILENT_FLAG)
    {
        FIXME("Silent flag not implemented\n");
    }

#ifdef __REACTOS__
    if (!object || object->type != KEY) return NTE_INVALID_HANDLE;
    if (is_private_blob(type) &&
        !(dword_property(object, NCRYPT_EXPORT_POLICY_PROPERTY, 0) & NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG))
        return NTE_NOT_SUPPORTED;
#endif
    return map_ntstatus(BCryptExportKey(object->key.bcrypt_key, NULL, type, output, output_len, ret_len, 0));
}

SECURITY_STATUS WINAPI NCryptIsAlgSupported(NCRYPT_PROV_HANDLE provider, const WCHAR *algid, DWORD flags)
{
    static const ULONG supported = BCRYPT_CIPHER_OPERATION |\
                                   BCRYPT_ASYMMETRIC_ENCRYPTION_OPERATION |\
                                   BCRYPT_SIGNATURE_OPERATION |\
                                   BCRYPT_SECRET_AGREEMENT_OPERATION;
    BCRYPT_ALGORITHM_IDENTIFIER *list;
    ULONG i, count;
    NTSTATUS status;

    TRACE("(%#Ix, %s, %#lx)\n", provider, wine_dbgstr_w(algid), flags);

    if (!provider) return NTE_INVALID_HANDLE;
    if (!algid) return HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER);
    if (flags == NCRYPT_SILENT_FLAG)
    {
        FIXME("Silent flag not implemented\n");
    }
    else if (flags)
    {
        WARN("Invalid flags %#lx\n", flags);
        return NTE_BAD_FLAGS;
    }
    if (!lstrcmpiW(BCRYPT_RSA_SIGN_ALGORITHM, algid)) return NTE_NOT_SUPPORTED;

    status = BCryptEnumAlgorithms(supported, &count, &list, 0);
    if (status != STATUS_SUCCESS)
    {
        ERR("Error retrieving algorithm list %#lx\n", status);
        return map_ntstatus(status);
    }

    status = STATUS_NOT_SUPPORTED;
    for (i = 0; i < count; i++)
    {
        if (!lstrcmpiW(list[i].pszName, algid))
        {
            status = STATUS_SUCCESS;
            break;
        }
    }

    BCryptFreeBuffer(list);
    return map_ntstatus(status);
}

BOOL WINAPI NCryptIsKeyHandle(NCRYPT_KEY_HANDLE hKey)
{
#ifdef __REACTOS__
    struct object *object = (struct object *)hKey;

    TRACE("(%#Ix)\n", hKey);
    return object && object->type == KEY;
#else
    FIXME("(%#Ix): stub\n", hKey);
    return FALSE;
#endif
}

SECURITY_STATUS WINAPI NCryptOpenKey(NCRYPT_PROV_HANDLE provider, NCRYPT_KEY_HANDLE *key,
                                     const WCHAR *name, DWORD keyspec, DWORD flags)
{
#ifdef __REACTOS__
    struct keystore_record record;
    struct object *object;
    SECURITY_STATUS ret;
    NTSTATUS status;
    enum algid algid;

    TRACE("(%#Ix, %p, %s, %#lx, %#lx)\n", provider, key, wine_dbgstr_w(name), keyspec, flags);

    if (!provider) return NTE_INVALID_HANDLE;
    if (!key || !name) return NTE_INVALID_PARAMETER;
    if (flags & ~(NCRYPT_MACHINE_KEY_FLAG | NCRYPT_SILENT_FLAG)) return NTE_BAD_FLAGS;
    *key = 0;
    if ((ret = keystore_read(name, !!(flags & NCRYPT_MACHINE_KEY_FLAG), &record))) return ret;
    if (!lstrcmpiW(record.algorithm, NCRYPT_RSA_ALGORITHM)) algid = RSA;
    else if (!lstrcmpiW(record.algorithm, NCRYPT_ECDSA_P256_ALGORITHM)) algid = ECDSA_P256;
    else
    {
        keystore_free_record(&record);
        return NTE_BAD_KEYSET;
    }
    if (!(object = create_key_object(algid, provider)))
    {
        keystore_free_record(&record);
        return NTE_NO_MEMORY;
    }
    status = BCryptImportKeyPair(algid == RSA ? BCRYPT_RSA_ALG_HANDLE : BCRYPT_ECDSA_P256_ALG_HANDLE, NULL,
                                 algid == RSA ? BCRYPT_RSAFULLPRIVATE_BLOB : BCRYPT_ECCPRIVATE_BLOB,
                                 &object->key.bcrypt_key, record.blob, record.blob_size, 0);
    if (status)
    {
        keystore_free_record(&record);
        NCryptFreeObject((NCRYPT_HANDLE)object);
        return map_ntstatus(status);
    }
    set_object_property(object, NCRYPT_LENGTH_PROPERTY, (BYTE *)&record.length, sizeof(record.length));
    set_object_property(object, BCRYPT_PUBLIC_KEY_LENGTH, (BYTE *)&record.length, sizeof(record.length));
    set_object_property(object, NCRYPT_EXPORT_POLICY_PROPERTY, (BYTE *)&record.export_policy,
                        sizeof(record.export_policy));
    set_object_property(object, NCRYPT_KEY_USAGE_PROPERTY, (BYTE *)&record.key_usage, sizeof(record.key_usage));
    keystore_free_record(&record);
    if (keystore_set_identity(object, name, flags & NCRYPT_MACHINE_KEY_FLAG))
    {
        NCryptFreeObject((NCRYPT_HANDLE)object);
        return NTE_NO_MEMORY;
    }
    object->key.finalized = TRUE;
    *key = (NCRYPT_KEY_HANDLE)object;
    return ERROR_SUCCESS;
#else
    FIXME("(%#Ix, %p, %s, %#lx, %#lx): stub\n", provider, key, wine_dbgstr_w(name), keyspec, flags);
    return NTE_NOT_SUPPORTED;
#endif
}

SECURITY_STATUS WINAPI NCryptOpenStorageProvider(NCRYPT_PROV_HANDLE *provider, const WCHAR *name, DWORD flags)
{
    struct object *object;
#ifdef __REACTOS__
    const WCHAR *provider_name = MS_KEY_STORAGE_PROVIDER;
    DWORD impl = NCRYPT_IMPL_SOFTWARE_FLAG | 0x20;

    TRACE("(%p, %s, %#lx)\n", provider, wine_dbgstr_w(name), flags);

    if (!provider) return NTE_INVALID_PARAMETER;
    *provider = 0;
    if (name && !lstrcmpiW(name, MS_PLATFORM_KEY_STORAGE_PROVIDER)) return NTE_DEVICE_NOT_READY;
    if (name && !lstrcmpiW(name, MS_SMART_CARD_KEY_STORAGE_PROVIDER))
    {
        provider_name = MS_SMART_CARD_KEY_STORAGE_PROVIDER;
        impl = NCRYPT_IMPL_HARDWARE_FLAG | NCRYPT_IMPL_SOFTWARE_FLAG | NCRYPT_IMPL_REMOVABLE_FLAG;
    }
    else if (name && lstrcmpiW(name, MS_KEY_STORAGE_PROVIDER))
    {
        return (SECURITY_STATUS)STATUS_NOT_FOUND;
    }
#else
    FIXME("(%p, %s, %#lx): stub\n", provider, wine_dbgstr_w(name), flags);
#endif

    if (!(object = allocate_object(STORAGE_PROVIDER)))
    {
        ERR("Error allocating memory.\n");
        return NTE_NO_MEMORY;
    }
#ifdef __REACTOS__
    set_object_property(object, NCRYPT_NAME_PROPERTY, (BYTE *)provider_name,
                        (lstrlenW(provider_name) + 1) * sizeof(WCHAR));
    set_object_property(object, NCRYPT_IMPL_TYPE_PROPERTY, (BYTE *)&impl, sizeof(impl));
#endif
    *provider = (NCRYPT_PROV_HANDLE)object;
    return ERROR_SUCCESS;
}

SECURITY_STATUS WINAPI NCryptSetProperty(NCRYPT_HANDLE handle, const WCHAR *name, BYTE *input, DWORD insize, DWORD flags)
{
    struct object *object = (struct object *)handle;

    TRACE("(%#Ix, %s, %p, %lu, %#lx)\n", handle, wine_dbgstr_w(name), input, insize, flags);
    if (flags) FIXME("flags %#lx not supported\n", flags);

    if (!object) return NTE_INVALID_HANDLE;
#ifdef __REACTOS__
    if (object->type == KEY)
    {
        static const WCHAR *const writable_properties[] =
        {
            NCRYPT_LENGTH_PROPERTY,
            NCRYPT_EXPORT_POLICY_PROPERTY,
            NCRYPT_KEY_USAGE_PROPERTY,
            NCRYPT_UI_POLICY_PROPERTY,
            NCRYPT_WINDOW_HANDLE_PROPERTY,
            NCRYPT_USE_CONTEXT_PROPERTY,
            NCRYPT_SECURITY_DESCR_PROPERTY,
            NCRYPT_CERTIFICATE_PROPERTY,
            NCRYPT_PIN_PROPERTY,
        };
        unsigned int i;

        if (!name) return NTE_INVALID_PARAMETER;
        for (i = 0; i < sizeof(writable_properties) / sizeof(writable_properties[0]); i++)
        {
            if (!lstrcmpW(name, writable_properties[i])) break;
        }
        if (i == sizeof(writable_properties) / sizeof(writable_properties[0])) return NTE_NOT_SUPPORTED;
    }
#endif
    if (name && !wcscmp(name, NCRYPT_PROVIDER_HANDLE_PROPERTY)) return NTE_NOT_SUPPORTED;
    return set_object_property(object, name, input, insize);
}

SECURITY_STATUS WINAPI NCryptSignHash(NCRYPT_KEY_HANDLE handle, void *padding, BYTE *value, DWORD value_len,
                                      BYTE *sig, DWORD sig_len, DWORD *ret_len, DWORD flags)
{
    struct object *object = (struct object *)handle;

    TRACE("(%#Ix, %p, %p, %lu, %p, %lu, %#lx)\n", handle, padding, value, value_len, sig, sig_len, flags);
    if (flags & NCRYPT_SILENT_FLAG) FIXME("Silent flag not implemented\n");

    if (!object || object->type != KEY) return NTE_INVALID_HANDLE;

    return map_ntstatus(BCryptSignHash(object->key.bcrypt_key, padding, value, value_len, sig, sig_len,
                                       ret_len, flags & ~NCRYPT_SILENT_FLAG));
}

SECURITY_STATUS WINAPI NCryptVerifySignature(NCRYPT_KEY_HANDLE handle, void *padding, BYTE *hash, DWORD hash_size,
                                             BYTE *signature, DWORD signature_size, DWORD flags)
{
    struct object *key_object = (struct object *)handle;

    TRACE("(%#Ix, %p, %p, %lu, %p, %lu, %#lx)\n", handle, padding, hash, hash_size, signature,
          signature_size, flags);

    if (!hash_size || !signature_size) return NTE_INVALID_PARAMETER;
    if (!hash || !signature) return HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER);
    if (!handle || key_object->type != KEY) return NTE_INVALID_HANDLE;

    if (key_object->key.algid < RSA)
    {
        FIXME("Symmetric keys not supported.\n");
        return NTE_NOT_SUPPORTED;
    }

#ifdef __REACTOS__
    if ((flags & NCRYPT_PAD_PKCS1_FLAG) && padding && ((BCRYPT_PKCS1_PADDING_INFO *)padding)->pszAlgId)
    {
        BCRYPT_ALG_HANDLE alg;
        DWORD length = 0, size;

        if (BCryptOpenAlgorithmProvider(&alg, ((BCRYPT_PKCS1_PADDING_INFO *)padding)->pszAlgId, NULL, 0))
            return NTE_INVALID_PARAMETER;
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (UCHAR *)&length, sizeof(length), &size, 0);
        BCryptCloseAlgorithmProvider(alg, 0);
        if (length != hash_size) return NTE_INVALID_PARAMETER;
    }
#endif

    return map_ntstatus(BCryptVerifySignature(key_object->key.bcrypt_key, padding, hash, hash_size, signature,
                                              signature_size, flags));
}
